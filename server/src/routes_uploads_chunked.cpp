#include "routes_uploads_chunked.h"

#include "httplib.h"
#include "audit_fields.h"
#include "audit_log.h"
#include "file_location_index.h"
#include "file_versions.h"
#include "gallery_meta.h"
#include "path_lock_manager.h"
#include "runtime_paths.h"
#include "storage_resolver.h"
#include "user_quota.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>

using json = nlohmann::json;

namespace {

ChunkedUploadRoutesContext g_upload_ctx;

std::string audit_safe_header_value(const std::string& value, std::size_t max_len) {
    std::string out;
    out.reserve(value.size());

    for (unsigned char ch : value) {
        if (ch < 0x20 || ch == 0x7f) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(ch));
        }
    }

    return pqnas::shorten(out, max_len);
}

pqnas::UsersRegistry* upload_users_ptr() {
    return g_upload_ctx.users;
}

bool upload_context_ok() {
    return g_upload_ctx.users &&
           g_upload_ctx.file_versions &&
           g_upload_ctx.cookie_key &&
           g_upload_ctx.require_user_auth &&
           g_upload_ctx.require_same_origin &&
           g_upload_ctx.require_no_live_lock_for_write &&
           g_upload_ctx.reply_json &&
           g_upload_ctx.reply_quota_error &&
           g_upload_ctx.user_dir_for_fp &&
           g_upload_ctx.random_b64url &&
           g_upload_ctx.now_epoch_sec &&
           g_upload_ctx.audit_append &&
           g_upload_ctx.upload_tiering_config &&
           g_upload_ctx.build_landing_abs_path &&
           g_upload_ctx.ensure_no_symlink_in_existing_path_prefix &&
           g_upload_ctx.record_user_file_activity;
}

void reply_json(httplib::Response& res, int status, const std::string& body) {
    if (g_upload_ctx.reply_json) {
        g_upload_ctx.reply_json(res, status, body);
        return;
    }

    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

bool require_user_auth_users_actor(const httplib::Request& req,
                                   httplib::Response& res,
                                   const unsigned char*,
                                   pqnas::UsersRegistry*,
                                   std::string* fp_hex,
                                   std::string* role) {
    return g_upload_ctx.require_user_auth(req, res, fp_hex, role);
}

bool require_same_origin_for_cookie_mutation(const httplib::Request& req,
                                             httplib::Response& res) {
    return g_upload_ctx.require_same_origin(req, res);
}

bool require_user_no_live_lock_for_write_local(pqnas::UsersRegistry&,
                                               const std::filesystem::path&,
                                               httplib::Response& res,
                                               const std::string& fp_hex,
                                               const std::string& rel_norm,
                                               const std::string& action,
                                               bool allow_when_locked) {
    return g_upload_ctx.require_no_live_lock_for_write(res, fp_hex, rel_norm, action, allow_when_locked);
}

bool reply_quota_error_v1(httplib::Response& res,
                          const std::string& fp_hex,
                          const pqnas::QuotaCheckResult& qc) {
    return g_upload_ctx.reply_quota_error(res, fp_hex, qc);
}

std::filesystem::path user_dir_for_fp(pqnas::UsersRegistry&,
                                      const std::string& fp_hex) {
    return g_upload_ctx.user_dir_for_fp(fp_hex);
}

std::string random_b64url(std::size_t n) {
    return g_upload_ctx.random_b64url(n);
}

std::int64_t now_epoch_sec() {
    return g_upload_ctx.now_epoch_sec();
}

void audit_append(const pqnas::AuditEvent& ev) {
    g_upload_ctx.audit_append(ev);
}

ChunkedUploadTieringConfig upload_tiering_config() {
    return g_upload_ctx.upload_tiering_config();
}

bool build_landing_abs_path(const std::string& pool_id,
                            const std::string& fp_hex,
                            const std::string& rel_norm,
                            std::filesystem::path* out_abs,
                            std::string* err) {
    return g_upload_ctx.build_landing_abs_path(pool_id, fp_hex, rel_norm, out_abs, err);
}

bool ensure_no_symlink_in_existing_path_prefix(const std::filesystem::path& path,
                                               std::string* err) {
    return g_upload_ctx.ensure_no_symlink_in_existing_path_prefix(path, err);
}

void record_user_file_activity_best_effort_local(pqnas::UsersRegistry&,
                                                 const std::string& fp_hex,
                                                 const std::string& event_name,
                                                 const std::string& logical_rel_path,
                                                 const std::string& item_type,
                                                 const std::string& aux,
                                                 std::uint64_t bytes,
                                                 int count,
                                                 const httplib::Request* req) {
    g_upload_ctx.record_user_file_activity(
        fp_hex,
        event_name,
        logical_rel_path,
        item_type,
        aux,
        bytes,
        count,
        req
    );
}

} // namespace

void register_chunked_upload_routes(
    httplib::Server& srv,
    const ChunkedUploadRoutesContext& ctx
) {
    g_upload_ctx = ctx;

    if (!upload_context_ok()) {
        srv.Post("/api/v4/uploads/start", [](const httplib::Request&, httplib::Response& res) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "chunked upload route context incomplete"}
            }.dump());
        });
        return;
    }

#define users_path (g_upload_ctx.users_path)
#define COOKIE_KEY (g_upload_ctx.cookie_key)
#define file_versions_index (*g_upload_ctx.file_versions)

// Transitional split from main.cpp.
// This file is intentionally included inside main() route-registration scope.
// TODO: convert to routes_uploads.cpp/.h after upload helpers have a clean context.

// ---- Chunked Upload API (user storage, My Files) ----
// Phase 1:
// - New-file uploads only (overwrite=false).
// - Designed to bypass proxy single-request body limits such as Cloudflare's ~100 MB cap.
// - Each chunk is <= 16 MiB, then finish assembles into the same storage/tiering metadata model.
// - Workspaces + Photo Gallery integration come later.
static constexpr std::uint64_t k_chunked_upload_chunk_bytes =
    16ull * 1024ull * 1024ull; // 16 MiB, safer for Firefox/Cloudflare/proxy upload paths

static constexpr std::uint64_t k_chunked_upload_max_total_bytes =
    64ull * 1024ull * 1024ull * 1024ull; // 64 GiB safety cap for one upload session

auto chunked_upload_root = []() -> std::filesystem::path {
    return std::filesystem::path(pqnas::data_root_dir()).parent_path() / "upload_sessions";
};

auto chunked_upload_id_ok = [](const std::string& s) -> bool {
    if (s.size() < 16 || s.size() > 96) return false;
    for (unsigned char c : s) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' || c == '-';
        if (!ok) return false;
    }
    return true;
};

auto chunked_upload_session_dir = [=](const std::string& fp_hex,
                                      const std::string& upload_id) -> std::filesystem::path {
    return chunked_upload_root() / fp_hex / upload_id;
};

auto chunked_active_temp_bytes_for_user = [=](const std::string& fp_hex) -> std::uint64_t {
    std::uint64_t total = 0;

    const std::filesystem::path root = chunked_upload_root() / fp_hex;

    std::error_code ec;
    if (!std::filesystem::exists(root, ec)) return 0;
    if (!std::filesystem::is_directory(root, ec)) return 0;

    const auto opts = std::filesystem::directory_options::skip_permission_denied;

    std::filesystem::recursive_directory_iterator it(root, opts, ec);
    const std::filesystem::recursive_directory_iterator end;

    for (; !ec && it != end; it.increment(ec)) {
        std::error_code st_ec;
        const auto st = std::filesystem::symlink_status(it->path(), st_ec);
        if (st_ec) continue;

        if (std::filesystem::is_symlink(st)) continue;
        if (!std::filesystem::is_regular_file(st)) continue;

        const std::uint64_t n = pqnas::file_size_u64_safe(it->path());

        if (std::numeric_limits<std::uint64_t>::max() - total < n) {
            return std::numeric_limits<std::uint64_t>::max();
        }

        total += n;
    }

    return total;
};

auto chunked_upload_chunk_name = [](std::uint64_t idx) -> std::string {
    std::string n = std::to_string((unsigned long long)idx);
    if (n.size() < 8) n = std::string(8 - n.size(), '0') + n;
    return n + ".part";
};

auto chunked_write_json_file = [](const std::filesystem::path& path,
                                  const json& j,
                                  std::string* err) -> bool {
    if (err) err->clear();

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        if (err) *err = "create_directories failed: " + ec.message();
        return false;
    }

    const auto tmp = path.parent_path() / (path.filename().string() + ".tmp." + random_b64url(8));

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.good()) {
            if (err) *err = "open tmp failed";
            return false;
        }

        const std::string body = j.dump(2);
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
        f.flush();

        if (!f.good()) {
            f.close();
            std::filesystem::remove(tmp, ec);
            if (err) *err = "write tmp failed";
            return false;
        }
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (err) *err = "rename failed: " + ec.message();
        return false;
    }

    return true;
};

auto chunked_read_json_file = [](const std::filesystem::path& path,
                                 json* out,
                                 std::string* err) -> bool {
    if (err) err->clear();
    if (!out) {
        if (err) *err = "null output";
        return false;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        if (err) *err = "cannot open file";
        return false;
    }

    std::string body((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) {
        if (err) *err = "read failed";
        return false;
    }

    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        if (err) *err = "invalid json";
        return false;
    }

    *out = std::move(j);
    return true;
};

auto chunked_json_u64 = [](const json& j,
                           const char* key,
                           std::uint64_t* out) -> bool {
    if (!out || !key) return false;
    if (!j.contains(key)) return false;

    try {
        const auto& v = j[key];

        if (v.is_number_unsigned()) {
            *out = v.get<std::uint64_t>();
            return true;
        }

        if (v.is_number_integer()) {
            long long x = v.get<long long>();
            if (x < 0) return false;
            *out = static_cast<std::uint64_t>(x);
            return true;
        }

        if (v.is_string()) {
            const std::string s = v.get<std::string>();
            if (s.empty()) return false;
            size_t idx = 0;
            unsigned long long x = std::stoull(s, &idx, 10);
            if (idx != s.size()) return false;
            *out = static_cast<std::uint64_t>(x);
            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
};

auto chunked_header_u64 = [](const httplib::Request& req,
                             const char* name,
                             std::uint64_t* out) -> bool {
    if (!out || !name) return false;

    auto it = req.headers.find(name);
    if (it == req.headers.end()) return false;

    try {
        const std::string& s = it->second;
        size_t idx = 0;
        unsigned long long v = std::stoull(s, &idx, 10);
        if (idx != s.size()) return false;
        *out = static_cast<std::uint64_t>(v);
        return true;
    } catch (...) {
        return false;
    }
};

auto chunked_expected_chunk_bytes = [](std::uint64_t size_bytes,
                                       std::uint64_t chunk_size,
                                       std::uint64_t chunks_total,
                                       std::uint64_t idx) -> std::uint64_t {
    if (chunks_total == 0) return 0;
    if (idx + 1 < chunks_total) return chunk_size;

    const std::uint64_t already = chunk_size * idx;
    if (already >= size_bytes) return 0;
    return size_bytes - already;
};

auto chunked_audit_headers = [=](const httplib::Request& req, pqnas::AuditEvent& ev) {
    ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;

    auto it_cf = req.headers.find("CF-Connecting-IP");
    if (it_cf != req.headers.end()) ev.f["cf_ip"] = audit_safe_header_value(it_cf->second, 120);

    auto it_xff = req.headers.find("X-Forwarded-For");
    if (it_xff != req.headers.end()) ev.f["xff"] = audit_safe_header_value(it_xff->second, 120);

    auto it_ua = req.headers.find("User-Agent");
    ev.f["ua"] = pqnas::shorten(it_ua == req.headers.end() ? "" : it_ua->second);
};

auto chunked_audit_fail = [=](const httplib::Request& req,
                              const std::string& fp_hex,
                              const std::string& event_name,
                              const std::string& reason,
                              int http,
                              const std::string& detail = "") {
    try {
        pqnas::AuditEvent ev;
        ev.event = event_name;
        ev.outcome = "fail";
        ev.f["fingerprint"] = fp_hex;
        ev.f["reason"] = reason;
        ev.f["http"] = std::to_string(http);
        if (!detail.empty()) ev.f["detail"] = pqnas::shorten(detail, 180);
        chunked_audit_headers(req, ev);
        audit_append(ev);
    } catch (...) {}
};

auto chunked_preflight_new_file = [=](const httplib::Request& req,
                                      httplib::Response& res,
                                      const std::string& fp_hex,
                                      const std::string& rel_norm,
                                      std::uint64_t size_bytes,
                                      bool overwrite,
                                      std::filesystem::path* out_abs,
                                      bool* out_tiering_write,
                                      ChunkedUploadTieringConfig* out_tier_cfg,
                                      std::string* out_old_physical_path) -> bool {
    if (out_abs) *out_abs = std::filesystem::path{};
    if (out_tiering_write) *out_tiering_write = false;
    if (out_tier_cfg) *out_tier_cfg = ChunkedUploadTieringConfig{};
    if (out_old_physical_path) out_old_physical_path->clear();

    auto fail = [&](const std::string& reason, int http, const json& body, const std::string& detail = "") -> bool {
        chunked_audit_fail(req, fp_hex, "v4.uploads_preflight_fail", reason, http, detail);
        reply_json(res, http, body.dump());
        return false;
    };

    std::string found_ancestor;
    std::string aerr;
    const bool ancestor_conflict =
        pqnas::any_file_ancestor_exists((*g_upload_ctx.users), fp_hex, rel_norm, &found_ancestor, &aerr);

    if (!aerr.empty()) {
        return fail("ancestor_check_failed", 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "ancestor conflict check failed"},
            {"detail", pqnas::shorten(aerr, 180)}
        }, aerr);
    }

    if (ancestor_conflict) {
        return fail("ancestor_is_file", 409, json{
            {"ok", false},
            {"error", "path_conflict"},
            {"message", "a parent path is an existing file"},
            {"ancestor", found_ancestor}
        }, found_ancestor);
    }

    auto* idx = pqnas::get_file_location_index();
    if (!idx) {
        return fail("metadata_index_missing", 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "metadata index missing"}
        });
    }

    {
        std::string derr;
        const bool has_descendants = idx->logical_dir_exists(fp_hex, rel_norm, &derr);

        if (!derr.empty()) {
            return fail("descendant_check_failed", 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "descendant conflict check failed"},
                {"detail", pqnas::shorten(derr, 180)}
            }, derr);
        }

        if (has_descendants) {
            return fail("path_has_descendants", 409, json{
                {"ok", false},
                {"error", "path_conflict"},
                {"message", "path already exists as a logical directory"},
                {"path", rel_norm}
            }, rel_norm);
        }
    }

    const std::filesystem::path user_dir = user_dir_for_fp((*g_upload_ctx.users), fp_hex);
    pqnas::QuotaCheckResult qc = pqnas::quota_check_for_upload_v1(
        (*g_upload_ctx.users), fp_hex, user_dir, rel_norm, size_bytes
    );

    if (!qc.ok) {
        if (reply_quota_error_v1(res, fp_hex, qc)) return false;

        return fail("quota_check_failed", 403, json{
            {"ok", false},
            {"error", qc.error.empty() ? "forbidden" : qc.error},
            {"message", "quota check failed"}
        }, qc.error);
    }

    ChunkedUploadTieringConfig tier_cfg = upload_tiering_config();

    std::filesystem::path final_abs = qc.abs_path;
    bool tiering_write = false;

    if (tier_cfg.enabled) {
        std::string terr;
        if (!build_landing_abs_path(tier_cfg.landing_pool_id, fp_hex, rel_norm, &final_abs, &terr)) {
            return fail("tiering_landing_pool_invalid", 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "tiering landing pool not found"},
                {"detail", pqnas::shorten(terr, 180)}
            }, terr);
        }

        tiering_write = true;
    }

    std::optional<pqnas::FileLocationRecord> existing_rec;
    std::string existing_rec_err;
    existing_rec = idx->get(fp_hex, rel_norm, &existing_rec_err);

    if (!existing_rec_err.empty()) {
        return fail("metadata_lookup_failed", 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "metadata lookup failed"},
            {"detail", pqnas::shorten(existing_rec_err, 180)}
        }, existing_rec_err);
    }

    bool physical_exists_at_target = false;
    std::uint64_t physical_existing_size = 0;
    std::int64_t physical_existing_mtime = 0;

    {
        std::error_code ec;
        auto st = std::filesystem::symlink_status(final_abs, ec);
        if (!ec && std::filesystem::exists(st)) {
            physical_exists_at_target = true;

            if (std::filesystem::is_symlink(st)) {
                return fail("target_symlink_not_supported", 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "target path exists as a symlink"},
                    {"path", rel_norm}
                }, final_abs.string());
            }

            if (!std::filesystem::is_regular_file(st)) {
                return fail("target_not_regular_file", 409, json{
                    {"ok", false},
                    {"error", "path_conflict"},
                    {"message", "target path exists and is not a regular file"},
                    {"path", rel_norm}
                }, final_abs.string());
            }

            physical_existing_size = pqnas::file_size_u64_safe(final_abs);

            auto ft = std::filesystem::last_write_time(final_abs, ec);
            if (!ec) {
                using namespace std::chrono;
                const auto sctp = time_point_cast<system_clock::duration>(
                    ft - std::filesystem::file_time_type::clock::now() + system_clock::now()
                );
                physical_existing_mtime = (std::int64_t)duration_cast<seconds>(sctp.time_since_epoch()).count();
            }
        } else if (ec && ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
            return fail("target_exists_check_failed", 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "target existence check failed"},
                {"detail", pqnas::shorten(ec.message(), 180)}
            }, ec.message());
        }
    }

    if (existing_rec.has_value() || physical_exists_at_target) {
        if (!overwrite) {
            json existing = json::object();

            if (existing_rec.has_value()) {
                existing["size_bytes"] = existing_rec->size_bytes;
                existing["mtime_epoch"] = existing_rec->mtime_epoch;
                existing["tier_state"] = existing_rec->tier_state;
                existing["current_pool"] = existing_rec->current_pool;
                existing["physical_path"] = existing_rec->physical_path;
            } else {
                existing["size_bytes"] = physical_existing_size;
                existing["mtime_epoch"] = physical_existing_mtime;
                existing["physical_path"] = final_abs.string();
            }

            return fail("file_exists", 409, json{
                {"ok", false},
                {"error", "file_exists"},
                {"message", "file already exists"},
                {"path", rel_norm},
                {"existing", existing}
            }, rel_norm);
        }

        if (out_old_physical_path) {
            if (existing_rec.has_value() && !existing_rec->physical_path.empty()) {
                *out_old_physical_path = existing_rec->physical_path;
            } else if (physical_exists_at_target) {
                *out_old_physical_path = final_abs.string();
            }
        }
    }

    if (out_abs) *out_abs = final_abs;
    if (out_tiering_write) *out_tiering_write = tiering_write;
    if (out_tier_cfg) *out_tier_cfg = tier_cfg;

    return true;
};

srv.Post("/api/v4/uploads/start", [=](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &(*g_upload_ctx.users), &fp_hex, &role)) return;

    if (!require_same_origin_for_cookie_mutation(req, res)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "origin_mismatch", 403);
        return;
    }

    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "invalid_json", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    const std::string rel_path = body.value("path", "");
    if (rel_path.empty()) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "missing_path", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing path"}
        }.dump());
        return;
    }

    bool overwrite = false;
    if (body.contains("overwrite") && body["overwrite"].is_boolean()) {
        overwrite = body["overwrite"].get<bool>();
    }

    std::uint64_t size_bytes = 0;
    if (!chunked_json_u64(body, "size_bytes", &size_bytes)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "missing_size_bytes", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "size_bytes required"}
        }.dump());
        return;
    }

    if (size_bytes > k_chunked_upload_max_total_bytes) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "chunked_total_limit_exceeded", 413);
        reply_json(res, 413, json{
            {"ok", false},
            {"error", "upload_too_large"},
            {"message", "chunked upload exceeds maximum total size"},
            {"size_bytes", size_bytes},
            {"max_bytes", k_chunked_upload_max_total_bytes}
        }.dump());
        return;
    }

    std::string rel_norm;
    {
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_path, &rel_norm, &nerr)) {
            chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "invalid_path", 400, nerr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }
    }


    if (!require_user_no_live_lock_for_write_local(
            (*g_upload_ctx.users), users_path, res, fp_hex, rel_norm, "write", false)) {
        return;
    }

    auto* plm = pqnas::get_path_lock_manager();
    auto write_guard = plm->lock_paths(fp_hex, {rel_norm});

    std::filesystem::path ignored_abs;
    bool ignored_tiering = false;
    ChunkedUploadTieringConfig ignored_tier_cfg;
    std::string ignored_old_physical_path;
    if (!chunked_preflight_new_file(req, res, fp_hex, rel_norm, size_bytes, overwrite,
                                    &ignored_abs, &ignored_tiering, &ignored_tier_cfg,
                                    &ignored_old_physical_path)) {
        return;
    }

    const std::uint64_t chunks_total =
        (size_bytes == 0)
            ? 0
            : ((size_bytes + k_chunked_upload_chunk_bytes - 1) / k_chunked_upload_chunk_bytes);

    const std::string upload_id = random_b64url(24);
    const std::filesystem::path dir = chunked_upload_session_dir(fp_hex, upload_id);

    std::error_code ec;
    std::filesystem::create_directories(dir / "chunks", ec);
    if (ec) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "mkdir_failed", 500, ec.message());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create upload session"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    json meta = json{
        {"ok", true},
        {"upload_id", upload_id},
        {"fingerprint", fp_hex},
        {"path", rel_norm},
        {"size_bytes", size_bytes},
        {"chunk_size", k_chunked_upload_chunk_bytes},
        {"chunks_total", chunks_total},
        {"overwrite", overwrite},
        {"created_epoch", now_epoch_sec()}
    };

    std::string werr;
    if (!chunked_write_json_file(dir / "meta.json", meta, &werr)) {
        std::filesystem::remove_all(dir, ec);
        chunked_audit_fail(req, fp_hex, "v4.uploads_start_fail", "write_meta_failed", 500, werr);
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to save upload session"},
            {"detail", pqnas::shorten(werr, 180)}
        }.dump());
        return;
    }

    try {
        pqnas::AuditEvent ev;
        ev.event = "v4.uploads_start_ok";
        ev.outcome = "ok";
        ev.f["fingerprint"] = fp_hex;
        ev.f["upload_id"] = upload_id;
        ev.f["path"] = pqnas::shorten(rel_norm, 200);
        ev.f["size_bytes"] = std::to_string((unsigned long long)size_bytes);
        ev.f["chunk_size"] = std::to_string((unsigned long long)k_chunked_upload_chunk_bytes);
        ev.f["chunks_total"] = std::to_string((unsigned long long)chunks_total);
        chunked_audit_headers(req, ev);
        audit_append(ev);
    } catch (...) {}

    reply_json(res, 200, json{
        {"ok", true},
        {"upload_id", upload_id},
        {"path", rel_norm},
        {"size_bytes", size_bytes},
        {"chunk_size", k_chunked_upload_chunk_bytes},
        {"chunks_total", chunks_total}
    }.dump());
});

srv.Put("/api/v4/uploads/chunk",
    [=](const httplib::Request& req,
        httplib::Response& res,
        const httplib::ContentReader& content_reader) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &(*g_upload_ctx.users), &fp_hex, &role)) return;

    if (!require_same_origin_for_cookie_mutation(req, res)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "origin_mismatch", 403);
        return;
    }

    const std::string upload_id = req.has_param("upload_id") ? req.get_param_value("upload_id") : "";
    if (!chunked_upload_id_ok(upload_id)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "invalid_upload_id", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid upload_id"}
        }.dump());
        return;
    }

    std::uint64_t chunk_index = 0;
    if (!req.has_param("index")) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "missing_index", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing index"}
        }.dump());
        return;
    }

    try {
        const std::string sidx = req.get_param_value("index");
        size_t pos = 0;
        unsigned long long v = std::stoull(sidx, &pos, 10);
        if (pos != sidx.size()) throw std::runtime_error("bad index");
        chunk_index = static_cast<std::uint64_t>(v);
    } catch (...) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "invalid_index", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid index"}
        }.dump());
        return;
    }

    const std::filesystem::path dir = chunked_upload_session_dir(fp_hex, upload_id);

    json meta;
    std::string rerr;
    if (!chunked_read_json_file(dir / "meta.json", &meta, &rerr)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "session_not_found", 404, rerr);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "upload session not found"}
        }.dump());
        return;
    }

    std::uint64_t size_bytes = 0;
    std::uint64_t chunk_size = 0;
    std::uint64_t chunks_total = 0;

    if (!chunked_json_u64(meta, "size_bytes", &size_bytes) ||
        !chunked_json_u64(meta, "chunk_size", &chunk_size) ||
        !chunked_json_u64(meta, "chunks_total", &chunks_total) ||
        chunk_size == 0 ||
        chunks_total == 0 ||
        chunk_index >= chunks_total) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "bad_session_meta", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "bad upload session metadata"}
        }.dump());
        return;
    }
    const std::string rel_norm = meta.value("path", "");
    {
        std::string check_norm;
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_norm, &check_norm, &nerr) ||
            check_norm != rel_norm) {
            chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "invalid_path", 400, nerr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid upload session path"}
            }.dump());
            return;
        }
    }
    const std::uint64_t expected_bytes =
        chunked_expected_chunk_bytes(size_bytes, chunk_size, chunks_total, chunk_index);

    std::uint64_t content_length = 0;
    if (!chunked_header_u64(req, "Content-Length", &content_length)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "missing_content_length", 411);
        reply_json(res, 411, json{
            {"ok", false},
            {"error", "length_required"},
            {"message", "Content-Length required"}
        }.dump());
        return;
    }

    if (content_length != expected_bytes || content_length > chunk_size) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "chunk_size_mismatch", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "chunk size mismatch"},
            {"content_length", content_length},
            {"expected_bytes", expected_bytes},
            {"chunk_size", chunk_size}
        }.dump());
        return;
    }
    // Quota pressure check for temporary chunk storage.
    //
    // Final quota is already checked at start + finish, but chunks live under
    // /srv/pqnas/upload_sessions instead of the user's real storage directory.
    // Without this check, abandoned chunk sessions can fill disk while bypassing
    // normal user quota accounting.
        auto* upload_plm = pqnas::get_path_lock_manager();
        auto upload_temp_quota_guard =
            upload_plm->lock_paths(fp_hex, {"__pqnas_upload_sessions_quota__"});

    std::uint64_t active_temp_bytes = chunked_active_temp_bytes_for_user(fp_hex);
    std::uint64_t temp_pressure_bytes = active_temp_bytes;

    if (std::numeric_limits<std::uint64_t>::max() - temp_pressure_bytes < expected_bytes) {
        temp_pressure_bytes = std::numeric_limits<std::uint64_t>::max();
    } else {
        temp_pressure_bytes += expected_bytes;
    }

    const std::filesystem::path user_dir = user_dir_for_fp((*g_upload_ctx.users), fp_hex);

    // Use a synthetic non-existing path so quota_check_for_upload_v1 does not
    // subtract the real destination file size. Temporary chunks are extra disk
    // pressure even when the upload will overwrite an existing live file.
    const std::string quota_probe_rel =
        ".pqnas_upload_quota_probe/" + upload_id + "/" + chunked_upload_chunk_name(chunk_index);

    pqnas::QuotaCheckResult temp_qc = pqnas::quota_check_for_upload_v1(
        (*g_upload_ctx.users),
        fp_hex,
        user_dir,
        quota_probe_rel,
        temp_pressure_bytes
    );

    if (!temp_qc.ok) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail",
                           "temp_upload_quota_exceeded", 413);

        if (reply_quota_error_v1(res, fp_hex, temp_qc)) return;

        reply_json(res, 413, json{
            {"ok", false},
            {"error", temp_qc.error.empty() ? "quota_exceeded" : temp_qc.error},
            {"message", "Upload chunk would exceed storage quota"},
            {"used_bytes", temp_qc.used_bytes},
            {"quota_bytes", temp_qc.quota_bytes},
            {"active_temp_bytes", active_temp_bytes},
            {"incoming_chunk_bytes", expected_bytes},
            {"would_used_bytes", temp_qc.would_used_bytes}
        }.dump());
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir / "chunks", ec);
    if (ec) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "mkdir_failed", 500, ec.message());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to prepare chunk directory"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    const std::filesystem::path final_chunk =
        dir / "chunks" / chunked_upload_chunk_name(chunk_index);

    const std::filesystem::path tmp_chunk =
        dir / "chunks" / (chunked_upload_chunk_name(chunk_index) + ".tmp." + random_b64url(8));

    std::uint64_t bytes_written = 0;
    bool stream_ok = true;
    std::string stream_err;

    try {
        std::ofstream f(tmp_chunk, std::ios::binary | std::ios::trunc);
        if (!f.good()) throw std::runtime_error("open chunk tmp failed");

        content_reader([&](const char* data, size_t len) {
            if (!stream_ok) return false;
            if (len == 0) return true;

            const std::uint64_t next = bytes_written + static_cast<std::uint64_t>(len);
            if (next < bytes_written || next > expected_bytes) {
                stream_ok = false;
                stream_err = "chunk_length_exceeded";
                return false;
            }

            f.write(data, static_cast<std::streamsize>(len));
            if (!f.good()) {
                stream_ok = false;
                stream_err = "write_chunk_failed";
                return false;
            }

            bytes_written = next;
            return true;
        });

        f.flush();
        if (!f.good()) throw std::runtime_error("flush chunk tmp failed");
        f.close();

        if (!stream_ok || bytes_written != expected_bytes) {
            std::filesystem::remove(tmp_chunk, ec);
            chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail",
                               stream_ok ? "chunk_length_mismatch" : stream_err, 400);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", stream_ok ? "chunk length mismatch" : stream_err},
                {"bytes_written", bytes_written},
                {"expected_bytes", expected_bytes}
            }.dump());
            return;
        }

        std::filesystem::rename(tmp_chunk, final_chunk, ec);
        if (ec) throw std::runtime_error("rename chunk failed: " + ec.message());

    } catch (const std::exception& e) {
        std::filesystem::remove(tmp_chunk, ec);
        chunked_audit_fail(req, fp_hex, "v4.uploads_chunk_fail", "write_failed", 500, e.what());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to write upload chunk"},
            {"detail", pqnas::shorten(e.what(), 180)}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"upload_id", upload_id},
        {"index", chunk_index},
        {"bytes", bytes_written}
    }.dump());
});

srv.Post("/api/v4/uploads/cancel", [=](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &(*g_upload_ctx.users), &fp_hex, &role)) return;

    if (!require_same_origin_for_cookie_mutation(req, res)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_cancel_fail", "origin_mismatch", 403);
        return;
    }

    json body = json::parse(req.body, nullptr, false);
    const std::string upload_id =
        (!body.is_discarded() && body.is_object()) ? body.value("upload_id", "") : "";

    if (!chunked_upload_id_ok(upload_id)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid upload_id"}
        }.dump());
        return;
    }

    std::error_code ec;
    const auto dir = chunked_upload_session_dir(fp_hex, upload_id);
    const auto n = std::filesystem::remove_all(dir, ec);

    if (ec) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_cancel_fail", "remove_failed", 500, ec.message());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to cancel upload"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"upload_id", upload_id},
        {"removed_entries", n}
    }.dump());
});

srv.Post("/api/v4/uploads/finish", [=](const httplib::Request& req, httplib::Response& res) {
    std::string fp_hex, role;
    if (!require_user_auth_users_actor(req, res, COOKIE_KEY, &(*g_upload_ctx.users), &fp_hex, &role)) return;

    if (!require_same_origin_for_cookie_mutation(req, res)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "origin_mismatch", 403);
        return;
    }

    json body = json::parse(req.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "invalid_json", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid json"}
        }.dump());
        return;
    }

    const std::string upload_id = body.value("upload_id", "");
    if (!chunked_upload_id_ok(upload_id)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "invalid_upload_id", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "invalid upload_id"}
        }.dump());
        return;
    }

    const std::filesystem::path dir = chunked_upload_session_dir(fp_hex, upload_id);

    json meta;
    std::string rerr;
    if (!chunked_read_json_file(dir / "meta.json", &meta, &rerr)) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "session_not_found", 404, rerr);
        reply_json(res, 404, json{
            {"ok", false},
            {"error", "not_found"},
            {"message", "upload session not found"}
        }.dump());
        return;
    }

    const std::string rel_norm = meta.value("path", "");
    bool overwrite = meta.value("overwrite", false);

    std::uint64_t size_bytes = 0;
    std::uint64_t chunk_size = 0;
    std::uint64_t chunks_total = 0;

    if (!chunked_json_u64(meta, "size_bytes", &size_bytes) ||
        !chunked_json_u64(meta, "chunk_size", &chunk_size) ||
        !chunked_json_u64(meta, "chunks_total", &chunks_total) ||
        chunk_size == 0) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "bad_session_meta", 400);
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "bad upload session metadata"}
        }.dump());
        return;
    }

    {
        std::string check_norm;
        std::string nerr;
        if (!pqnas::normalize_user_rel_path_strict(rel_norm, &check_norm, &nerr) || check_norm != rel_norm) {
            chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "invalid_path", 400, nerr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "invalid path"}
            }.dump());
            return;
        }
    }

    // Verify all chunks before touching final storage.
    for (std::uint64_t i = 0; i < chunks_total; ++i) {
        const auto chunk_path = dir / "chunks" / chunked_upload_chunk_name(i);
        const std::uint64_t expected =
            chunked_expected_chunk_bytes(size_bytes, chunk_size, chunks_total, i);

        std::error_code ec;
        auto st = std::filesystem::symlink_status(chunk_path, ec);
        if (ec || !std::filesystem::exists(st) || std::filesystem::is_symlink(st) || !std::filesystem::is_regular_file(st)) {
            chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "missing_chunk", 400,
                               "index=" + std::to_string((unsigned long long)i));
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "missing_chunk"},
                {"message", "upload chunk missing"},
                {"index", i}
            }.dump());
            return;
        }

        const std::uint64_t actual = pqnas::file_size_u64_safe(chunk_path);
        if (actual != expected) {
            chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "chunk_size_mismatch", 400,
                               "index=" + std::to_string((unsigned long long)i));
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_chunk"},
                {"message", "upload chunk has wrong size"},
                {"index", i},
                {"expected_bytes", expected},
                {"actual_bytes", actual}
            }.dump());
            return;
        }
    }

    if (!require_user_no_live_lock_for_write_local(
            (*g_upload_ctx.users), users_path, res, fp_hex, rel_norm, "write", false)) {
        return;
    }

    auto* plm = pqnas::get_path_lock_manager();
    auto write_guard = plm->lock_paths(fp_hex, {rel_norm});

    std::filesystem::path out_abs;
    bool tiering_write = false;
    ChunkedUploadTieringConfig tier_cfg;

    std::string old_physical_path;
    if (!chunked_preflight_new_file(req, res, fp_hex, rel_norm, size_bytes, overwrite,
                                    &out_abs, &tiering_write, &tier_cfg,
                                    &old_physical_path)) {
        return;
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(out_abs.parent_path(), &serr)) {
            chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "symlink_not_supported", 400, serr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"},
                {"detail", serr}
            }.dump());
            return;
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(out_abs.parent_path(), ec);
    if (ec) {
        chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "mkdir_failed", 500, ec.message());
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "failed to create directories"},
            {"detail", pqnas::shorten(ec.message(), 180)}
        }.dump());
        return;
    }

    {
        std::string serr;
        if (!ensure_no_symlink_in_existing_path_prefix(out_abs.parent_path(), &serr)) {
            chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "symlink_not_supported", 400, serr);
            reply_json(res, 400, json{
                {"ok", false},
                {"error", "bad_request"},
                {"message", "symlinks not supported"},
                {"detail", serr}
            }.dump());
            return;
        }
    }

    const std::filesystem::path tmp =
        out_abs.parent_path() /
        (out_abs.filename().string() + ".chunked." + random_b64url(8) + ".tmp");

    std::uint64_t assembled_bytes = 0;

    try {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.good()) throw std::runtime_error("open assembled tmp failed");

        std::array<char, 1024 * 1024> buf{};

        for (std::uint64_t i = 0; i < chunks_total; ++i) {
            const auto chunk_path = dir / "chunks" / chunked_upload_chunk_name(i);
            std::ifstream in(chunk_path, std::ios::binary);
            if (!in.good()) {
                throw std::runtime_error("open chunk failed index=" + std::to_string((unsigned long long)i));
            }

            while (in.good()) {
                in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                const std::streamsize n = in.gcount();
                if (n > 0) {
                    out.write(buf.data(), n);
                    if (!out.good()) throw std::runtime_error("write assembled tmp failed");
                    assembled_bytes += static_cast<std::uint64_t>(n);
                }
            }

            if (!in.eof()) {
                throw std::runtime_error("read chunk failed index=" + std::to_string((unsigned long long)i));
            }
        }

        out.flush();
        if (!out.good()) throw std::runtime_error("flush assembled tmp failed");
        out.close();

        if (assembled_bytes != size_bytes) {
            throw std::runtime_error(
                "assembled size mismatch expected=" +
                std::to_string((unsigned long long)size_bytes) +
                " got=" +
                std::to_string((unsigned long long)assembled_bytes)
            );
        }

        {
            std::error_code dst_ec;
            auto dst_st = std::filesystem::symlink_status(out_abs, dst_ec);
            if (!dst_ec && std::filesystem::exists(dst_st)) {
                if (std::filesystem::is_symlink(dst_st)) {
                    std::filesystem::remove(tmp, ec);
                    chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "target_symlink_not_supported", 409, out_abs.string());
                    reply_json(res, 409, json{
                        {"ok", false},
                        {"error", "path_conflict"},
                        {"message", "target path exists as a symlink"},
                        {"path", rel_norm}
                    }.dump());
                    return;
                }

                if (!std::filesystem::is_regular_file(dst_st)) {
                    std::filesystem::remove(tmp, ec);
                    chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "target_not_regular_file", 409, out_abs.string());
                    reply_json(res, 409, json{
                        {"ok", false},
                        {"error", "path_conflict"},
                        {"message", "target path exists and is not a regular file"},
                        {"path", rel_norm}
                    }.dump());
                    return;
                }

                if (!overwrite) {
                    std::filesystem::remove(tmp, ec);
                    chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "file_exists", 409, rel_norm);
                    reply_json(res, 409, json{
                        {"ok", false},
                        {"error", "file_exists"},
                        {"message", "file already exists"},
                        {"path", rel_norm}
                    }.dump());
                    return;
                }

                if (old_physical_path.empty()) {
                    old_physical_path = out_abs.string();
                }
            }

            if (dst_ec && dst_ec != std::make_error_code(std::errc::no_such_file_or_directory)) {
                throw std::runtime_error("target existence check failed: " + dst_ec.message());
            }
        }

        if (overwrite && !old_physical_path.empty()) {
            const std::filesystem::path user_dir = user_dir_for_fp((*g_upload_ctx.users), fp_hex);

            pqnas::PreserveLiveFileVersionParams vp;
            vp.scope_type = "user";
            vp.scope_id = fp_hex;
            vp.scope_root = user_dir;
            vp.logical_rel_path = rel_norm;
            vp.live_abs_path = std::filesystem::path(old_physical_path);
            vp.event_kind = "overwrite_preserve";
            vp.actor_fp = fp_hex;
            vp.users = upload_users_ptr();

            pqnas::FileVersionRec vrec;
            std::string verr;
            if (!file_versions_index.preserve_live_file_version(vp, &vrec, &verr)) {
                std::filesystem::remove(tmp, ec);

                chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "version_preserve_failed", 500, verr);
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "failed to preserve previous version"},
                    {"detail", pqnas::shorten(verr, 180)}
                }.dump());
                return;
            }
        }

        std::filesystem::rename(tmp, out_abs, ec);
        if (ec) throw std::runtime_error("rename assembled file failed: " + ec.message());

        auto* idx = pqnas::get_file_location_index();
        if (!idx) {
            std::filesystem::remove(out_abs, ec);
            throw std::runtime_error("metadata index missing");
        }

        const std::int64_t now_ts = now_epoch_sec();

        pqnas::FileLocationRecord rec;
        rec.fp = fp_hex;
        rec.logical_rel_path = rel_norm;
        rec.current_pool = tiering_write ? tier_cfg.landing_pool_id : "";
        rec.physical_path = out_abs.string();
        rec.tier_state = tiering_write ? "landing" : "capacity";
        rec.size_bytes = assembled_bytes;
        rec.mtime_epoch = now_ts;
        rec.created_epoch = now_ts;
        rec.updated_epoch = now_ts;
        rec.version = 1;

        std::string merr;
        if (!idx->upsert_landing_file(rec, &merr)) {
            std::filesystem::remove(out_abs, ec);
            throw std::runtime_error("metadata upsert failed: " + merr);
        }

        if (auto* gidx = pqnas::get_gallery_meta_index()) {
            std::string gerr;
            (void)gidx->touch_file_facts("user", fp_hex, rel_norm, assembled_bytes, now_ts, now_ts, &gerr);
        }

        if (tiering_write) {
            pqnas::AuditEvent tev;
            tev.event = "storage.tiering_upload_landed";
            tev.outcome = "ok";
            tev.f["fingerprint"] = fp_hex;
            tev.f["path"] = pqnas::shorten(rel_norm, 200);
            tev.f["tier_state"] = "landing";
            tev.f["pool_id"] = tier_cfg.landing_pool_id;
            tev.f["physical_path"] = pqnas::shorten(out_abs.string(), 220);
            tev.f["bytes"] = std::to_string((unsigned long long)assembled_bytes);
            tev.f["source"] = "chunked";
            chunked_audit_headers(req, tev);
            audit_append(tev);
        }

        if (!old_physical_path.empty()) {
            const std::string new_physical_path = out_abs.string();
            if (old_physical_path != new_physical_path) {
                std::error_code old_rm_ec;
                std::filesystem::remove(old_physical_path, old_rm_ec);
                if (old_rm_ec) {
                    try {
                        pqnas::AuditEvent ev;
                        ev.event = "v4.uploads_old_physical_cleanup_fail";
                        ev.outcome = "warn";
                        ev.f["fingerprint"] = fp_hex;
                        ev.f["path"] = pqnas::shorten(rel_norm, 200);
                        ev.f["old_physical_path"] = pqnas::shorten(old_physical_path, 220);
                        ev.f["new_physical_path"] = pqnas::shorten(new_physical_path, 220);
                        ev.f["detail"] = pqnas::shorten(old_rm_ec.message(), 180);
                        chunked_audit_headers(req, ev);
                        audit_append(ev);
                    } catch (...) {}
                }
            }
        }

        std::filesystem::remove_all(dir, ec);

        try {
            pqnas::AuditEvent ev;
            ev.event = "v4.uploads_finish_ok";
            ev.outcome = "ok";
            ev.f["fingerprint"] = fp_hex;
            ev.f["upload_id"] = upload_id;
            ev.f["path"] = pqnas::shorten(rel_norm, 200);
            ev.f["bytes"] = std::to_string((unsigned long long)assembled_bytes);
            ev.f["chunks_total"] = std::to_string((unsigned long long)chunks_total);
            ev.f["tier_state"] = tiering_write ? "landing" : "capacity";
            chunked_audit_headers(req, ev);
            audit_append(ev);
        } catch (...) {}

        record_user_file_activity_best_effort_local(
            (*g_upload_ctx.users),
            fp_hex,
            "file.uploaded",
            rel_norm,
            "file",
            "",
            assembled_bytes,
            1,
            &req
        );

        reply_json(res, 200, json{
            {"ok", true},
            {"chunked", true},
            {"fingerprint_hex", fp_hex},
            {"path", rel_norm},
            {"bytes", assembled_bytes},
            {"overwrite", overwrite}
        }.dump());
        return;

    } catch (const std::exception& e) {
        std::filesystem::remove(tmp, ec);

        chunked_audit_fail(req, fp_hex, "v4.uploads_finish_fail", "finish_failed", 500, e.what());

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "chunked upload finish failed"},
            {"detail", pqnas::shorten(e.what(), 180)}
        }.dump());
        return;
    }
});


// ---- Files API (user storage) ----


#undef file_versions_index
#undef COOKIE_KEY
#undef users_path
}
