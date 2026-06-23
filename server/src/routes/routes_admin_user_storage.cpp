#include "routes_admin_user_storage.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"
#include "users_registry.h"
#include "workspaces.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminUserStorageRoutesContext& c) {
    return c.users &&
           c.workspaces &&
           c.require_admin_auth &&
           c.require_same_origin &&
           c.reply_json &&
           c.trim_copy &&
           c.is_valid_fingerprint_hex &&
           c.normalize_storage_pool_id &&
           c.storage_pool_mount_by_id &&
           c.data_root_dir &&
           c.default_root_rel_for_fp &&
           c.is_safe_rel_path &&
           c.statvfs_path &&
           c.dir_size_bytes_best_effort &&
           c.ensure_dir_exists &&
           c.sum_allocated_workspace_quota_on_pool &&
           c.now_iso_utc &&
           c.audit_append;
}

void reply_json_local(
    const AdminUserStorageRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) {
        c.reply_json(res, status, body.dump());
        return;
    }

    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

std::string req_ip(const httplib::Request& req) {
    return req.remote_addr.empty() ? "?" : req.remote_addr;
}

std::uint64_t add_sat(std::uint64_t a, std::uint64_t b) {
    if (std::numeric_limits<std::uint64_t>::max() - a < b) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return a + b;
}

std::uint64_t sum_allocated_user_quota_on_pool(
    const AdminUserStorageRoutesContext& c,
    const std::string& want_pool_id,
    const std::string& exclude_fp
) {
    const std::string want_pool = c.normalize_storage_pool_id(want_pool_id);
    std::uint64_t total = 0;

    for (const auto& kv : c.users->snapshot()) {
        const auto& it = kv.second;

        if (!exclude_fp.empty() && it.fingerprint == exclude_fp) continue;
        if (it.storage_state != "allocated") continue;

        const std::string user_pool = c.normalize_storage_pool_id(it.storage_pool_id);
        if (user_pool != want_pool) continue;

        total = add_sat(total, static_cast<std::uint64_t>(it.quota_bytes));
    }

    return total;
}

void audit_best_effort(
    const AdminUserStorageRoutesContext& c,
    const std::string& event,
    const std::string& outcome,
    const std::map<std::string, std::string>& fields
) {
    try {
        pqnas::AuditEvent ev;
        ev.event = event;
        ev.outcome = outcome;
        ev.f = fields;
        c.audit_append(ev);
    } catch (...) {
    }
}

bool path_is_under_root(
    const std::filesystem::path& root,
    const std::filesystem::path& child
) {
    const auto dr = root.lexically_normal();
    const auto ud = child.lexically_normal();
    const auto rel_to_root = ud.lexically_relative(dr);

    if (rel_to_root.empty()) return false;

    for (const auto& part : rel_to_root) {
        if (part == "..") return false;
    }

    return true;
}

} // namespace

void register_admin_user_storage_routes(
    httplib::Server& srv,
    const AdminUserStorageRoutesContext& ctx
) {
    const AdminUserStorageRoutesContext c = ctx;

    srv.Post("/api/v4/admin/users/storage",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_auth(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            res.set_header("Cache-Control", "no-store");

            if (!c.workspaces->load(c.workspaces_path)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "workspaces_load_failed"},
                    {"message", "failed to reload workspaces"}
                });
                return;
            }

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid json"}
                });
                return;
            }

            const std::string fp = j.value("fingerprint", "");
            if (fp.empty()) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint"}
                });
                return;
            }

            if (!c.is_valid_fingerprint_hex(fp)) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid fingerprint format"}
                });
                return;
            }

            std::string pool_id = j.value("pool_id", "");
            pool_id = c.trim_copy(pool_id);
            if (pool_id.empty()) pool_id = "default";

            if (!c.users->exists(fp)) {
                reply_json_local(c, res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "user not found"}
                });
                return;
            }

            const bool force = j.value("force", false);

            double quota_gb_d = 0.0;
            try {
                if (!j.contains("quota_gb")) {
                    reply_json_local(c, res, 400, json{
                        {"ok", false},
                        {"error", "bad_request"},
                        {"message", "missing quota_gb"}
                    });
                    return;
                }

                const auto& v = j["quota_gb"];
                if (v.is_number_integer()) {
                    quota_gb_d = static_cast<double>(v.get<long long>());
                } else if (v.is_number_unsigned()) {
                    quota_gb_d = static_cast<double>(v.get<unsigned long long>());
                } else if (v.is_number_float()) {
                    quota_gb_d = v.get<double>();
                } else {
                    reply_json_local(c, res, 400, json{
                        {"ok", false},
                        {"error", "bad_request"},
                        {"message", "quota_gb must be a number"}
                    });
                    return;
                }
            } catch (...) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid quota_gb"}
                });
                return;
            }

            if (quota_gb_d < 0.0) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "quota_gb must be >= 0"}
                });
                return;
            }

            const long double bytes_ld =
                static_cast<long double>(quota_gb_d) *
                1024.0L * 1024.0L * 1024.0L;

            if (bytes_ld > static_cast<long double>(std::numeric_limits<std::uint64_t>::max())) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "quota_gb too large"}
                });
                return;
            }

            const std::uint64_t quota_bytes =
                static_cast<std::uint64_t>(bytes_ld + 0.5L);

            const std::string now_iso = c.now_iso_utc();

            auto cur = c.users->get(fp);
            if (!cur.has_value()) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "user lookup failed"}
                });
                return;
            }

            pqnas::UserRec u = *cur;

            if (u.status != "enabled") {
                audit_best_effort(c, "admin.user_storage_allocate_refused", "fail", {
                    {"fingerprint", fp},
                    {"reason", "user_not_approved"},
                    {"status", pqnas::shorten(u.status, 40)},
                    {"ts", now_iso},
                    {"actor_fp", actor_fp},
                    {"ip", req_ip(req)}
                });

                reply_json_local(c, res, 403, json{
                    {"ok", false},
                    {"error", "user_not_approved"},
                    {"message", "refusing to allocate storage: user is not enabled/approved"},
                    {"status", u.status}
                });
                return;
            }

            const std::string prev_state = u.storage_state;
            const std::uint64_t prev_quota = u.quota_bytes;
            const std::string prev_root = u.root_rel;

            const bool already_allocated = (u.storage_state == "allocated");

            if (already_allocated && !force) {
                audit_best_effort(c, "admin.user_storage_allocate_refused", "fail", {
                    {"fingerprint", fp},
                    {"reason", "already_allocated"},
                    {"ts", now_iso},
                    {"actor_fp", actor_fp},
                    {"ip", req_ip(req)}
                });

                reply_json_local(c, res, 409, json{
                    {"ok", false},
                    {"error", "already_allocated"},
                    {"message", "storage is already allocated; use force=true to change quota"},
                    {"storage_state", u.storage_state},
                    {"quota_bytes", u.quota_bytes},
                    {"root_rel", u.root_rel}
                });
                return;
            }

            std::filesystem::path data_root = std::filesystem::path(c.data_root_dir());
            std::string pool_mount;

            if (pool_id != "default") {
                std::string err;
                std::string mp;
                if (!c.storage_pool_mount_by_id(pool_id, &mp, &err)) {
                    reply_json_local(c, res, 404, json{
                        {"ok", false},
                        {"error", "pool_not_found"},
                        {"message", "pool_id not found"},
                        {"pool_id", pool_id},
                        {"detail", err}
                    });
                    return;
                }

                pool_mount = mp;
                data_root = std::filesystem::path(mp) / "data";
            }

            {
                const std::string effective_pool_id = c.normalize_storage_pool_id(pool_id);

                std::uint64_t pool_total_bytes = 0;
                std::uint64_t pool_free_bytes = 0;

                const std::string stat_path =
                    (effective_pool_id == "default")
                        ? data_root.string()
                        : std::filesystem::path(pool_mount).string();

                if (!c.statvfs_path(stat_path, &pool_total_bytes, &pool_free_bytes)) {
                    audit_best_effort(c, "admin.user_storage_allocate_refused", "fail", {
                        {"fingerprint", fp},
                        {"reason", "pool_statvfs_failed"},
                        {"pool_id", effective_pool_id},
                        {"path", pqnas::shorten(stat_path, 200)},
                        {"ts", now_iso},
                        {"actor_fp", actor_fp},
                        {"ip", req_ip(req)}
                    });

                    reply_json_local(c, res, 500, json{
                        {"ok", false},
                        {"error", "pool_statvfs_failed"},
                        {"message", "failed to read target pool capacity"},
                        {"pool_id", effective_pool_id},
                        {"path", stat_path}
                    });
                    return;
                }

                const std::uint64_t allocated_user_bytes =
                    sum_allocated_user_quota_on_pool(c, effective_pool_id, fp);

                const std::uint64_t allocated_workspace_bytes =
                    c.sum_allocated_workspace_quota_on_pool(effective_pool_id, "");

                const std::uint64_t allocated_other_bytes =
                    add_sat(allocated_user_bytes, allocated_workspace_bytes);

                const std::uint64_t would_total_bytes =
                    add_sat(allocated_other_bytes, quota_bytes);

                if (would_total_bytes > pool_total_bytes) {
                    audit_best_effort(c, "admin.user_storage_allocate_refused", "fail", {
                        {"fingerprint", fp},
                        {"reason", "pool_quota_overcommit"},
                        {"pool_id", effective_pool_id},
                        {"requested_quota_bytes", std::to_string(static_cast<unsigned long long>(quota_bytes))},
                        {"allocated_other_bytes", std::to_string(static_cast<unsigned long long>(allocated_other_bytes))},
                        {"would_total_bytes", std::to_string(static_cast<unsigned long long>(would_total_bytes))},
                        {"pool_total_bytes", std::to_string(static_cast<unsigned long long>(pool_total_bytes))},
                        {"pool_free_bytes", std::to_string(static_cast<unsigned long long>(pool_free_bytes))},
                        {"allocated_user_bytes", std::to_string(static_cast<unsigned long long>(allocated_user_bytes))},
                        {"allocated_workspace_bytes", std::to_string(static_cast<unsigned long long>(allocated_workspace_bytes))},
                        {"ts", now_iso},
                        {"actor_fp", actor_fp},
                        {"ip", req_ip(req)}
                    });

                    reply_json_local(c, res, 409, json{
                        {"ok", false},
                        {"error", "pool_quota_overcommit"},
                        {"message", "requested quota would exceed pool capacity"},
                        {"pool_id", effective_pool_id},
                        {"requested_quota_bytes", quota_bytes},
                        {"allocated_other_bytes", allocated_other_bytes},
                        {"would_total_bytes", would_total_bytes},
                        {"pool_total_bytes", pool_total_bytes},
                        {"allocated_user_bytes", allocated_user_bytes},
                        {"allocated_workspace_bytes", allocated_workspace_bytes},
                        {"pool_free_bytes", pool_free_bytes}
                    });
                    return;
                }
            }

            const std::string canonical_root_rel = c.default_root_rel_for_fp(fp);
            if (canonical_root_rel.empty() || !c.is_safe_rel_path(canonical_root_rel)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "computed root_rel is unsafe"},
                    {"root_rel", canonical_root_rel}
                });
                return;
            }

            const std::filesystem::path udir = data_root / canonical_root_rel;
            const std::filesystem::path parent = udir.parent_path();

            if (!path_is_under_root(data_root, udir)) {
                audit_best_effort(c, "admin.user_storage_bad_path", "fail", {
                    {"fingerprint", fp},
                    {"data_root", pqnas::shorten(data_root.string(), 200)},
                    {"user_dir", pqnas::shorten(udir.string(), 200)},
                    {"ts", now_iso},
                    {"actor_fp", actor_fp},
                    {"ip", req_ip(req)}
                });

                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "refusing to allocate: user_dir is not under data_root"}
                });
                return;
            }

            {
                std::string fs_err;
                if (!c.ensure_dir_exists(parent, &fs_err)) {
                    audit_best_effort(c, "admin.user_storage_mkdir_failed", "fail", {
                        {"fingerprint", fp},
                        {"path", parent.string()},
                        {"detail", pqnas::shorten(fs_err, 180)},
                        {"ts", now_iso},
                        {"actor_fp", actor_fp},
                        {"ip", req_ip(req)}
                    });

                    reply_json_local(c, res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to create storage root"},
                        {"detail", fs_err}
                    });
                    return;
                }
            }

            {
                std::string fs_err;
                if (!c.ensure_dir_exists(udir, &fs_err)) {
                    audit_best_effort(c, "admin.user_storage_mkdir_failed", "fail", {
                        {"fingerprint", fp},
                        {"path", udir.string()},
                        {"detail", pqnas::shorten(fs_err, 180)},
                        {"ts", now_iso},
                        {"actor_fp", actor_fp},
                        {"ip", req_ip(req)}
                    });

                    reply_json_local(c, res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "failed to create user directory"},
                        {"detail", fs_err}
                    });
                    return;
                }
            }

            {
                const std::uint64_t used_bytes = c.dir_size_bytes_best_effort(udir);

                if (quota_bytes < used_bytes) {
                    const std::string effective_pool_id = c.normalize_storage_pool_id(pool_id);

                    audit_best_effort(c, "admin.user_storage_allocate_refused", "fail", {
                        {"fingerprint", fp},
                        {"reason", "quota_below_used_bytes"},
                        {"pool_id", effective_pool_id},
                        {"requested_quota_bytes", std::to_string(static_cast<unsigned long long>(quota_bytes))},
                        {"used_bytes", std::to_string(static_cast<unsigned long long>(used_bytes))},
                        {"user_dir", pqnas::shorten(udir.string(), 200)},
                        {"ts", now_iso},
                        {"actor_fp", actor_fp},
                        {"ip", req_ip(req)}
                    });

                    reply_json_local(c, res, 409, json{
                        {"ok", false},
                        {"error", "quota_below_used_bytes"},
                        {"message", "requested quota is below current storage usage"},
                        {"fingerprint", fp},
                        {"pool_id", effective_pool_id},
                        {"requested_quota_bytes", quota_bytes},
                        {"used_bytes", used_bytes}
                    });
                    return;
                }
            }

            const std::string root_rel = canonical_root_rel;

            audit_best_effort(
                c,
                already_allocated
                    ? "admin.user_storage_dir_verified"
                    : "admin.user_storage_dir_created",
                "ok",
                {
                    {"fingerprint", fp},
                    {"path", udir.string()},
                    {"ts", now_iso},
                    {"actor_fp", actor_fp},
                    {"ip", req_ip(req)}
                }
            );

            u.storage_state = "allocated";
            u.quota_bytes = quota_bytes;
            u.root_rel = root_rel;
            u.storage_set_at = now_iso;
            u.storage_set_by = actor_fp;
            u.storage_pool_id = (pool_id == "default") ? "" : pool_id;

            const bool ok_upsert = c.users->upsert(u);
            const bool ok_save = ok_upsert ? c.users->save(c.users_path) : false;

            std::map<std::string, std::string> audit_fields = {
                {"fingerprint", fp},
                {"ts", now_iso},
                {"actor_fp", actor_fp},
                {"ip", req_ip(req)},
                {"quota_gb", pqnas::shorten(std::to_string(quota_gb_d), 32)},
                {"quota_bytes", pqnas::shorten(std::to_string(static_cast<unsigned long long>(quota_bytes)), 32)},
                {"root_rel", pqnas::shorten(u.root_rel, 160)},
                {"user_dir", pqnas::shorten(udir.string(), 200)}
            };

            if (force) audit_fields["force"] = "true";
            if (!prev_state.empty()) audit_fields["prev_storage_state"] = pqnas::shorten(prev_state, 40);
            if (prev_quota != 0) {
                audit_fields["prev_quota_bytes"] =
                    pqnas::shorten(std::to_string(static_cast<unsigned long long>(prev_quota)), 32);
            }
            if (!prev_root.empty()) audit_fields["prev_root_rel"] = pqnas::shorten(prev_root, 160);

            audit_best_effort(
                c,
                already_allocated
                    ? "admin.user_storage_updated"
                    : "admin.user_storage_allocated",
                (ok_upsert && ok_save) ? "ok" : "fail",
                audit_fields
            );

            if (!ok_upsert) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "upsert failed"}
                });
                return;
            }

            if (!ok_save) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "users save failed"}
                });
                return;
            }

            reply_json_local(c, res, 200, json{
                {"ok", true},
                {"fingerprint", fp},
                {"pool_id", pool_id},
                {"storage_state", u.storage_state},
                {"quota_bytes", u.quota_bytes},
                {"root_rel", u.root_rel},
                {"storage_set_at", u.storage_set_at},
                {"storage_set_by", u.storage_set_by}
            });
        }
    );
}
