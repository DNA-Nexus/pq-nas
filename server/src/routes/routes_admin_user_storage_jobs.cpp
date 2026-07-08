#include "routes_admin_user_storage_jobs.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"
#include "users_registry.h"
#include "user_storage_migration.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

std::string trim_copy_local(std::string s) {
    auto is_ws = [](unsigned char c) {
        return std::isspace(c) != 0;
    };

    auto first = std::find_if_not(s.begin(), s.end(), [&](char c) {
        return is_ws(static_cast<unsigned char>(c));
    });

    auto last = std::find_if_not(s.rbegin(), s.rend(), [&](char c) {
        return is_ws(static_cast<unsigned char>(c));
    }).base();

    if (first >= last) return {};
    return std::string(first, last);
}

bool is_sha256_hex_lower_local(const std::string& s) {
    if (s.size() != 64) return false;
    for (unsigned char c : s) {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

bool context_ok(const AdminUserStorageJobsRoutesContext& c) {
    return c.users &&
           c.require_admin &&
           c.require_same_origin &&
           c.reply_json &&
           c.audit_append &&
           c.enqueue_migration_job &&
           c.read_migration_record &&
           c.enqueue_cleanup_job &&
           c.read_cleanup_record;
}

void reply_json_ctx(
    const AdminUserStorageJobsRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}


std::string normalize_pool_id_route_local(std::string s) {
    s = trim_copy_local(std::move(s));
    if (s == "DEFAULT" || s == "Default" || s == "default") return "default";
    return s;
}

std::filesystem::path storage_batch_dir_local(const std::string& users_path) {
    return std::filesystem::path(users_path).parent_path() / "admin_user_storage_batches";
}

std::string now_iso_route_local() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t tt = system_clock::to_time_t(now);
    std::tm tm {};
    gmtime_r(&tt, &tm);

    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return os.str();
}

std::string random_storage_batch_id_local() {
    // Security: batch_id is not an auth secret, but random ids avoid predictable
    // admin batch URLs and accidental collisions.
    std::random_device rd;
    std::ostringstream os;
    os << std::hex << std::setfill('0');

    for (int i = 0; i < 8; ++i) {
        const auto v = static_cast<std::uint32_t>(rd());
        os << std::setw(8) << v;
    }

    return os.str();
}

bool safe_storage_batch_id_local(const std::string& s) {
    return is_sha256_hex_lower_local(s);
}

void prune_storage_batches_best_effort_local(const std::string& users_path) {
    const auto dir = storage_batch_dir_local(users_path);

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return;

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    for (const auto& ent : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) return;

        std::error_code ec2;
        if (!ent.is_regular_file(ec2) || ec2) continue;

        const auto p = ent.path();
        if (p.extension() != ".json") continue;
        if (!safe_storage_batch_id_local(p.stem().string())) continue;

        files.push_back({std::filesystem::last_write_time(p, ec2), p});
    }

    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    // Growth control: keep only newest durable admin batch records.
    constexpr std::size_t kKeepNewestBatches = 100;
    for (std::size_t i = kKeepNewestBatches; i < files.size(); ++i) {
        std::filesystem::remove(files[i].second, ec);
    }
}

bool write_storage_batch_json_local(const std::string& users_path,
                                    const std::string& batch_id,
                                    const json& batch,
                                    std::string* err) {
    if (err) err->clear();

    if (!safe_storage_batch_id_local(batch_id)) {
        if (err) *err = "bad batch_id";
        return false;
    }

    const auto dir = storage_batch_dir_local(users_path);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (err) *err = "failed to create batch dir: " + ec.message();
        return false;
    }

    const auto dst = dir / (batch_id + ".json");
    const auto tmp = dir / (batch_id + ".tmp");

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good()) {
            if (err) *err = "failed to open temp batch file";
            return false;
        }

        out << batch.dump(2) << "\n";
        if (!out.good()) {
            if (err) *err = "failed to write temp batch file";
            return false;
        }
    }

    std::filesystem::rename(tmp, dst, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);
        if (err) *err = "failed to replace batch file: " + ec.message();
        return false;
    }

    prune_storage_batches_best_effort_local(users_path);
    return true;
}

bool read_storage_batch_json_local(const std::string& users_path,
                                   const std::string& batch_id,
                                   json* out,
                                   std::string* err) {
    if (err) err->clear();
    if (out) *out = json::object();

    if (!safe_storage_batch_id_local(batch_id)) {
        if (err) *err = "bad_batch_id";
        return false;
    }

    const auto path = storage_batch_dir_local(users_path) / (batch_id + ".json");
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) {
        if (err) *err = "batch_not_found";
        return false;
    }

    try {
        json j;
        in >> j;
        if (!j.is_object()) {
            if (err) *err = "bad_batch_json";
            return false;
        }

        if (out) *out = std::move(j);
        return true;
    } catch (const std::exception& e) {
        if (err) *err = std::string("bad_batch_json: ") + e.what();
        return false;
    }
}

json storage_batch_summary_local(const json& batch) {
    json s = json::object();
    s["batch_id"] = batch.value("batch_id", "");
    s["kind"] = batch.value("kind", "");
    s["created_at"] = batch.value("created_at", "");
    s["updated_at"] = batch.value("updated_at", "");
    s["expected_from_pool_id"] = batch.value("expected_from_pool_id", "");
    s["target_pool_id"] = batch.value("target_pool_id", "");

    std::size_t queued = 0;
    std::size_t skipped = 0;
    std::size_t failed = 0;
    std::size_t cleanup_queued = 0;

    const auto& items = batch.contains("items") ? batch["items"] : json::array();
    if (items.is_array()) {
        s["items"] = items.size();

        for (const auto& item : items) {
            if (!item.is_object()) continue;

            const std::string state = item.value("state", "");
            if (state == "queued") ++queued;
            else if (state == "skipped") ++skipped;
            else if (state == "failed") ++failed;

            if (!item.value("cleanup_job_id", "").empty()) ++cleanup_queued;
        }
    } else {
        s["items"] = 0;
    }

    s["queued"] = queued;
    s["skipped"] = skipped;
    s["failed"] = failed;
    s["cleanup_queued"] = cleanup_queued;
    return s;
}

json read_recent_storage_batch_summaries_local(const std::string& users_path) {
    const auto dir = storage_batch_dir_local(users_path);
    json out = json::array();

    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec) return out;

    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> files;
    for (const auto& ent : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) return out;

        std::error_code ec2;
        if (!ent.is_regular_file(ec2) || ec2) continue;

        const auto p = ent.path();
        if (p.extension() != ".json") continue;
        if (!safe_storage_batch_id_local(p.stem().string())) continue;

        files.push_back({std::filesystem::last_write_time(p, ec2), p});
    }

    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });

    constexpr std::size_t kMaxReturnedBatches = 25;
    for (std::size_t i = 0; i < files.size() && i < kMaxReturnedBatches; ++i) {
        std::ifstream in(files[i].second, std::ios::binary);
        if (!in.good()) continue;

        try {
            json batch;
            in >> batch;
            if (batch.is_object()) out.push_back(storage_batch_summary_local(batch));
        } catch (...) {
            continue;
        }
    }

    return out;
}

json json_array_or_empty_local(const json& j, const char* key) {
    if (!j.contains(key) || !j.at(key).is_array()) return json::array();
    return j.at(key);
}

std::set<std::string> parse_fingerprint_filter_local(const json& j) {
    std::set<std::string> out;
    const auto arr = json_array_or_empty_local(j, "fingerprints");

    for (const auto& v : arr) {
        if (!v.is_string()) continue;

        const auto fp = trim_copy_local(v.get<std::string>());
        if (!fp.empty()) out.insert(fp);
    }

    return out;
}


} // namespace

void register_admin_user_storage_jobs_routes(
    httplib::Server& srv,
    const AdminUserStorageJobsRoutesContext& ctx
) {
    const AdminUserStorageJobsRoutesContext c = ctx;

    srv.Get("/api/v4/admin/users/storage_batches",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            res.set_header("Cache-Control", "no-store");
            reply(200, json{
                {"ok", true},
                {"batches", read_recent_storage_batch_summaries_local(c.users_path)}
            });
        }
    );

    srv.Get("/api/v4/admin/users/storage_batch",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            res.set_header("Cache-Control", "no-store");

            const std::string batch_id = trim_copy_local(req.get_param_value("batch_id"));
            json batch;
            std::string err;
            if (!read_storage_batch_json_local(c.users_path, batch_id, &batch, &err)) {
                reply(err == "batch_not_found" ? 404 : 400, json{
                    {"ok", false},
                    {"error", err.empty() ? "batch_read_failed" : err},
                    {"message", "storage batch not found or invalid"}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"batch", batch}
            });
        }
    );

    srv.Post("/api/v4/admin/users/bulk_migrate_storage",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string pool_id = normalize_pool_id_route_local(j.value("pool_id", ""));
            const std::string raw_expected = trim_copy_local(j.value("expected_from_pool_id", "all"));
            const std::string expected_from_pool_id = raw_expected.empty()
                ? "all"
                : normalize_pool_id_route_local(raw_expected);

            const auto fingerprints = json_array_or_empty_local(j, "fingerprints");

            constexpr std::size_t kBulkMaxUsers = 100;
            if (pool_id.empty() || fingerprints.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing pool_id or fingerprints"}
                });
                return;
            }

            if (fingerprints.size() > kBulkMaxUsers) {
                reply(413, json{
                    {"ok", false},
                    {"error", "too_many_users"},
                    {"message", "bulk migration is capped at 100 users per request"},
                    {"limit", kBulkMaxUsers}
                });
                return;
            }

            const std::string batch_id = random_storage_batch_id_local();
            const std::string now = now_iso_route_local();

            json batch = json::object({
                {"batch_id", batch_id},
                {"kind", "bulk_migration"},
                {"created_at", now},
                {"updated_at", now},
                {"actor_fp", actor_fp},
                {"target_pool_id", pool_id},
                {"expected_from_pool_id", expected_from_pool_id},
                {"items", json::array()}
            });

            std::set<std::string> seen;
            std::size_t queued = 0;
            std::size_t skipped = 0;
            std::size_t failed = 0;

            for (const auto& v : fingerprints) {
                json item = json::object();

                if (!v.is_string()) {
                    ++skipped;
                    item["state"] = "skipped";
                    item["reason"] = "bad_fingerprint_value";
                    batch["items"].push_back(item);
                    continue;
                }

                const std::string fp = trim_copy_local(v.get<std::string>());
                item["fingerprint"] = fp;

                if (fp.empty()) {
                    ++skipped;
                    item["state"] = "skipped";
                    item["reason"] = "missing_fingerprint";
                    batch["items"].push_back(item);
                    continue;
                }

                if (!seen.insert(fp).second) {
                    ++skipped;
                    item["state"] = "skipped";
                    item["reason"] = "duplicate_fingerprint";
                    batch["items"].push_back(item);
                    continue;
                }

                pqnas::UserStorageMigrationPlan plan;
                std::string resolve_err;
                if (!pqnas::resolve_user_storage_migration(*c.users, c.users_path, fp, pool_id, &plan, &resolve_err)) {
                    ++skipped;
                    item["state"] = "skipped";
                    item["reason"] = "resolve_failed";
                    item["detail"] = pqnas::shorten(resolve_err, 180);
                    batch["items"].push_back(item);
                    continue;
                }

                item["from_pool_id"] = plan.from_pool_id;
                item["to_pool_id"] = plan.to_pool_id;
                item["root_rel"] = plan.root_rel;

                if (expected_from_pool_id != "all" && plan.from_pool_id != expected_from_pool_id) {
                    ++skipped;
                    item["state"] = "skipped";
                    item["reason"] = "source_pool_mismatch";
                    item["expected_from_pool_id"] = expected_from_pool_id;
                    batch["items"].push_back(item);
                    continue;
                }

                if (plan.from_pool_id == plan.to_pool_id) {
                    ++skipped;
                    item["state"] = "skipped";
                    item["reason"] = "same_pool";
                    batch["items"].push_back(item);
                    continue;
                }

                try {
                    json out = c.enqueue_migration_job(actor_fp, fp, pool_id, req.remote_addr);
                    const std::string job_id = trim_copy_local(out.value("job_id", ""));

                    if (!is_sha256_hex_lower_local(job_id)) {
                        throw std::runtime_error("migration job_id missing or invalid");
                    }

                    ++queued;
                    item["state"] = "queued";
                    item["migration_job_id"] = job_id;
                } catch (const std::exception& e) {
                    ++failed;
                    item["state"] = "failed";
                    item["reason"] = "enqueue_failed";
                    item["detail"] = pqnas::shorten(e.what(), 180);
                }

                batch["items"].push_back(item);
            }

            batch["updated_at"] = now_iso_route_local();
            batch["counts"] = json{
                {"total", batch["items"].size()},
                {"queued", queued},
                {"skipped", skipped},
                {"failed", failed}
            };

            std::string write_err;
            if (!write_storage_batch_json_local(c.users_path, batch_id, batch, &write_err)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "batch_write_failed"},
                    {"message", write_err}
                });
                return;
            }

            pqnas::AuditEvent ev;
            ev.event = "admin.user_storage_bulk_migration_batch_created";
            ev.outcome = failed == 0 ? "ok" : "partial";
            ev.f["actor_fp"] = actor_fp;
            ev.f["batch_id"] = batch_id;
            ev.f["to_pool_id"] = pool_id;
            ev.f["expected_from_pool_id"] = expected_from_pool_id;
            ev.f["queued"] = std::to_string(queued);
            ev.f["skipped"] = std::to_string(skipped);
            ev.f["failed"] = std::to_string(failed);
            ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
            c.audit_append(ev);

            reply(200, json{
                {"ok", true},
                {"batch_id", batch_id},
                {"queued", queued},
                {"skipped", skipped},
                {"failed", failed},
                {"batch", batch}
            });
        }
    );

    srv.Post("/api/v4/admin/users/bulk_cleanup_old_storage",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string batch_id = trim_copy_local(j.value("batch_id", ""));
            if (!safe_storage_batch_id_local(batch_id)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_batch_id"},
                    {"message", "missing or invalid batch_id"}
                });
                return;
            }

            json batch;
            std::string read_err;
            if (!read_storage_batch_json_local(c.users_path, batch_id, &batch, &read_err)) {
                reply(read_err == "batch_not_found" ? 404 : 400, json{
                    {"ok", false},
                    {"error", read_err.empty() ? "batch_read_failed" : read_err},
                    {"message", "storage batch not found or invalid"}
                });
                return;
            }

            if (!batch.contains("items") || !batch["items"].is_array()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_batch"},
                    {"message", "batch does not contain items"}
                });
                return;
            }

            const std::set<std::string> fp_filter = parse_fingerprint_filter_local(j);
            const bool restrict_fps = !fp_filter.empty();

            std::size_t queued = 0;
            std::size_t skipped = 0;
            std::size_t failed = 0;
            json results = json::array();

            for (auto& item : batch["items"]) {
                if (!item.is_object()) continue;

                json result = json::object();
                const std::string fp = trim_copy_local(item.value("fingerprint", ""));
                result["fingerprint"] = fp;

                if (fp.empty()) {
                    ++skipped;
                    result["state"] = "skipped";
                    result["reason"] = "missing_fingerprint";
                    results.push_back(result);
                    continue;
                }

                if (restrict_fps && fp_filter.count(fp) == 0) {
                    continue;
                }

                const std::string migration_job_id = trim_copy_local(item.value("migration_job_id", ""));
                const std::string expected_active_pool_id = normalize_pool_id_route_local(item.value("to_pool_id", ""));
                const std::string old_pool_id = normalize_pool_id_route_local(item.value("from_pool_id", ""));

                result["expected_active_pool_id"] = expected_active_pool_id;
                result["old_pool_id"] = old_pool_id;

                if (!is_sha256_hex_lower_local(migration_job_id)) {
                    ++skipped;
                    result["state"] = "skipped";
                    result["reason"] = "missing_migration_job_id";
                    results.push_back(result);
                    continue;
                }

                if (expected_active_pool_id.empty() || old_pool_id.empty() || expected_active_pool_id == old_pool_id) {
                    ++skipped;
                    result["state"] = "skipped";
                    result["reason"] = "bad_pool_pair";
                    results.push_back(result);
                    continue;
                }

                json migration_record;
                std::string status_err;
                if (!c.read_migration_record(migration_job_id, &migration_record, &status_err)) {
                    ++skipped;
                    result["state"] = "skipped";
                    result["reason"] = "migration_job_not_found";
                    result["detail"] = pqnas::shorten(status_err, 180);
                    results.push_back(result);
                    continue;
                }

                const std::string migration_state = migration_record.value("state", "");
                if (migration_state != "done") {
                    ++skipped;
                    result["state"] = "skipped";
                    result["reason"] = "migration_not_done";
                    result["migration_state"] = migration_state;
                    results.push_back(result);
                    continue;
                }

                const std::string resolved_dest = normalize_pool_id_route_local(
                    migration_record.value("resolved_dest_pool_id",
                        migration_record.value("requested_target_pool_id", ""))
                );
                const std::string resolved_source = normalize_pool_id_route_local(
                    migration_record.value("resolved_source_pool_id",
                        migration_record.value("from_pool_id", ""))
                );

                if (!resolved_dest.empty() && resolved_dest != expected_active_pool_id) {
                    ++skipped;
                    result["state"] = "skipped";
                    result["reason"] = "migration_dest_mismatch";
                    result["resolved_dest_pool_id"] = resolved_dest;
                    results.push_back(result);
                    continue;
                }

                if (!resolved_source.empty() && resolved_source != old_pool_id) {
                    ++skipped;
                    result["state"] = "skipped";
                    result["reason"] = "migration_source_mismatch";
                    result["resolved_source_pool_id"] = resolved_source;
                    results.push_back(result);
                    continue;
                }

                try {
                    json out = c.enqueue_cleanup_job(
                        actor_fp,
                        fp,
                        expected_active_pool_id,
                        old_pool_id,
                        req.remote_addr
                    );

                    const std::string cleanup_job_id = trim_copy_local(out.value("job_id", ""));
                    if (!is_sha256_hex_lower_local(cleanup_job_id)) {
                        throw std::runtime_error("cleanup job_id missing or invalid");
                    }

                    ++queued;
                    item["cleanup_job_id"] = cleanup_job_id;
                    item["cleanup_state"] = "queued";
                    item["cleanup_queued_at"] = now_iso_route_local();

                    result["state"] = "queued";
                    result["cleanup_job_id"] = cleanup_job_id;
                } catch (const std::exception& e) {
                    ++failed;
                    result["state"] = "failed";
                    result["reason"] = "cleanup_enqueue_failed";
                    result["detail"] = pqnas::shorten(e.what(), 180);
                }

                results.push_back(result);
            }

            batch["updated_at"] = now_iso_route_local();
            batch["cleanup_counts"] = json{
                {"queued", queued},
                {"skipped", skipped},
                {"failed", failed}
            };

            std::string write_err;
            if (!write_storage_batch_json_local(c.users_path, batch_id, batch, &write_err)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "batch_write_failed"},
                    {"message", write_err}
                });
                return;
            }

            pqnas::AuditEvent ev;
            ev.event = "admin.user_storage_bulk_cleanup_batch_queued";
            ev.outcome = failed == 0 ? "ok" : "partial";
            ev.f["actor_fp"] = actor_fp;
            ev.f["batch_id"] = batch_id;
            ev.f["queued"] = std::to_string(queued);
            ev.f["skipped"] = std::to_string(skipped);
            ev.f["failed"] = std::to_string(failed);
            ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
            c.audit_append(ev);

            reply(200, json{
                {"ok", true},
                {"batch_id", batch_id},
                {"queued", queued},
                {"skipped", skipped},
                {"failed", failed},
                {"results", results},
                {"batch", batch}
            });
        }
    );

    srv.Post("/api/v4/admin/users/migrate_storage",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string fp = trim_copy_local(j.value("fingerprint", ""));
            std::string pool_id = trim_copy_local(j.value("pool_id", ""));
            if (pool_id == "DEFAULT" || pool_id == "Default" || pool_id == "default") {
                pool_id = "default";
            }

            if (fp.empty() || pool_id.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint or pool_id"}
                });
                return;
            }

            pqnas::UserStorageMigrationPlan plan;
            std::string resolve_err;
            if (!pqnas::resolve_user_storage_migration(*c.users, c.users_path, fp, pool_id, &plan, &resolve_err)) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["to_pool_id"] = pool_id;
                ev.f["reason"] = "resolve_failed";
                if (!resolve_err.empty()) ev.f["detail"] = pqnas::shorten(resolve_err, 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                int http = 400;
                if (resolve_err == "user_missing" || resolve_err == "storage_unallocated") http = 404;

                reply(http, json{
                    {"ok", false},
                    {"error", "resolve_failed"},
                    {"message", "user storage migration rejected"},
                    {"detail", resolve_err}
                });
                return;
            }

            if (plan.from_pool_id == plan.to_pool_id) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["from_pool_id"] = plan.from_pool_id;
                ev.f["to_pool_id"] = plan.to_pool_id;
                ev.f["reason"] = "same_pool";
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(409, json{
                    {"ok", false},
                    {"error", "same_pool"},
                    {"message", "source and destination pool are the same"}
                });
                return;
            }

            try {
                json out = c.enqueue_migration_job(actor_fp, fp, pool_id, req.remote_addr);

                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_job_created";
                ev.outcome = "ok";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["from_pool_id"] = plan.from_pool_id;
                ev.f["to_pool_id"] = plan.to_pool_id;
                ev.f["job_id"] = out.value("job_id", "");
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                if (c.reply_json) c.reply_json(res, 200, out.dump());
                else fallback_reply_json(res, 200, out.dump());
                return;
            } catch (const std::exception& e) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_migration_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["to_pool_id"] = pool_id;
                ev.f["reason"] = "enqueue_failed";
                ev.f["detail"] = pqnas::shorten(e.what(), 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(500, json{
                    {"ok", false},
                    {"error", "enqueue_failed"},
                    {"message", "failed to create migration job"}
                });
                return;
            }
        }
    );

    srv.Get("/api/v4/admin/users/migrate_storage_status",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            res.set_header("Cache-Control", "no-store");

            const std::string job_id = trim_copy_local(req.get_param_value("job_id"));
            if (!is_sha256_hex_lower_local(job_id)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_job_id"},
                    {"message", "invalid job_id"}
                });
                return;
            }

            json rec;
            std::string err;
            if (!c.read_migration_record(job_id, &rec, &err)) {
                reply(404, json{
                    {"ok", false},
                    {"error", err.empty() ? "not_found" : err},
                    {"message", "migration job not found"}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"job", rec}
            });
        }
    );

    srv.Post("/api/v4/admin/users/cleanup_old_storage",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string fp = trim_copy_local(j.value("fingerprint", ""));
            const std::string expected_active_pool_id = trim_copy_local(j.value("expected_active_pool_id", ""));
            const std::string old_pool_id = trim_copy_local(j.value("old_pool_id", ""));

            if (fp.empty() || expected_active_pool_id.empty() || old_pool_id.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint, expected_active_pool_id or old_pool_id"}
                });
                return;
            }

            if (expected_active_pool_id == old_pool_id) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_cleanup_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["expected_active_pool_id"] = expected_active_pool_id;
                ev.f["old_pool_id"] = old_pool_id;
                ev.f["reason"] = "same_pool";
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(409, json{
                    {"ok", false},
                    {"error", "same_pool"},
                    {"message", "expected active pool and old pool must differ"}
                });
                return;
            }

            try {
                json out = c.enqueue_cleanup_job(
                    actor_fp,
                    fp,
                    expected_active_pool_id,
                    old_pool_id,
                    req.remote_addr
                );

                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_cleanup_job_created";
                ev.outcome = "ok";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["expected_active_pool_id"] = expected_active_pool_id;
                ev.f["old_pool_id"] = old_pool_id;
                ev.f["job_id"] = out.value("job_id", "");
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                if (c.reply_json) c.reply_json(res, 200, out.dump());
                else fallback_reply_json(res, 200, out.dump());
            } catch (const std::exception& e) {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_storage_cleanup_rejected";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["actor_fp"] = actor_fp;
                ev.f["expected_active_pool_id"] = expected_active_pool_id;
                ev.f["old_pool_id"] = old_pool_id;
                ev.f["reason"] = "enqueue_failed";
                ev.f["detail"] = pqnas::shorten(e.what(), 180);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(500, json{
                    {"ok", false},
                    {"error", "enqueue_failed"},
                    {"message", e.what()}
                });
            }
        }
    );

    srv.Get("/api/v4/admin/users/cleanup_old_storage_status",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage job route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin(req, res, &actor_fp)) return;
            (void)actor_fp;

            res.set_header("Cache-Control", "no-store");

            const std::string job_id = trim_copy_local(req.get_param_value("job_id"));
            if (!is_sha256_hex_lower_local(job_id)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_job_id"},
                    {"message", "invalid job_id"}
                });
                return;
            }

            json rec;
            std::string err;
            if (!c.read_cleanup_record(job_id, &rec, &err)) {
                reply(404, json{
                    {"ok", false},
                    {"error", err.empty() ? "not_found" : err},
                    {"message", "cleanup job record not found"}
                });
                return;
            }

            reply(200, json{
                {"ok", true},
                {"job", rec}
            });
        }
    );
}
