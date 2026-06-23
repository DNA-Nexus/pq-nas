#include "routes_admin_user_storage_preview.h"

#include "httplib.h"
#include "users_registry.h"
#include "workspaces.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminUserStoragePreviewRoutesContext& c) {
    return c.users &&
           c.workspaces &&
           c.require_admin_cookie &&
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
           c.sum_allocated_workspace_quota_on_pool;
}

void reply_json_local(
    const AdminUserStoragePreviewRoutesContext& c,
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

std::uint64_t add_sat(std::uint64_t a, std::uint64_t b) {
    if (std::numeric_limits<std::uint64_t>::max() - a < b) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return a + b;
}

std::uint64_t sum_allocated_user_quota_on_pool(
    const AdminUserStoragePreviewRoutesContext& c,
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

} // namespace

void register_admin_user_storage_preview_routes(
    httplib::Server& srv,
    const AdminUserStoragePreviewRoutesContext& ctx
) {
    const AdminUserStoragePreviewRoutesContext c = ctx;

    srv.Get("/api/v4/admin/users/storage_preview",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user storage preview route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_cookie(req, res, &actor_fp)) return;

            res.set_header("Cache-Control", "no-store");

            if (!c.workspaces->load(c.workspaces_path)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "workspaces_load_failed"},
                    {"message", "failed to reload workspaces"}
                });
                return;
            }

            const std::string fp = c.trim_copy(req.get_param_value("fingerprint"));
            if (fp.empty() || !c.is_valid_fingerprint_hex(fp)) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing or invalid fingerprint"}
                });
                return;
            }

            std::string pool_id = c.trim_copy(req.get_param_value("pool_id"));
            pool_id = c.normalize_storage_pool_id(pool_id);

            auto cur = c.users->get(fp);
            if (!cur.has_value()) {
                reply_json_local(c, res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "user not found"}
                });
                return;
            }

            const pqnas::UserRec u = *cur;

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

            std::uint64_t pool_total_bytes = 0;
            std::uint64_t pool_free_bytes = 0;

            const std::string stat_path =
                (pool_id == "default")
                    ? data_root.string()
                    : std::filesystem::path(pool_mount).string();

            if (!c.statvfs_path(stat_path, &pool_total_bytes, &pool_free_bytes)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "pool_statvfs_failed"},
                    {"message", "failed to read target pool capacity"},
                    {"pool_id", pool_id},
                    {"path", stat_path}
                });
                return;
            }

            std::uint64_t used_bytes = 0;
            {
                std::error_code ec;
                if (std::filesystem::exists(udir, ec) &&
                    std::filesystem::is_directory(udir, ec)) {
                    used_bytes = c.dir_size_bytes_best_effort(udir);
                }
            }

            const std::uint64_t allocated_user_bytes =
                sum_allocated_user_quota_on_pool(c, pool_id, fp);

            const std::uint64_t allocated_workspace_bytes =
                c.sum_allocated_workspace_quota_on_pool(pool_id, "");

            const std::uint64_t allocated_other_bytes =
                add_sat(allocated_user_bytes, allocated_workspace_bytes);

            const std::uint64_t current_quota_bytes =
                (u.storage_state == "allocated")
                    ? static_cast<std::uint64_t>(u.quota_bytes)
                    : 0;

            const std::uint64_t allocated_total_bytes =
                add_sat(allocated_other_bytes, current_quota_bytes);

            const std::uint64_t remaining_allocatable_bytes =
                (pool_total_bytes > allocated_other_bytes)
                    ? (pool_total_bytes - allocated_other_bytes)
                    : 0;

            reply_json_local(c, res, 200, json{
                {"ok", true},
                {"fingerprint", fp},
                {"pool_id", pool_id},
                {"used_bytes", used_bytes},
                {"current_quota_bytes", current_quota_bytes},
                {"pool_total_bytes", pool_total_bytes},
                {"pool_free_bytes", pool_free_bytes},
                {"allocated_other_bytes", allocated_other_bytes},
                {"allocated_total_bytes", allocated_total_bytes},
                {"allocated_user_bytes", allocated_user_bytes},
                {"allocated_workspace_bytes", allocated_workspace_bytes},
                {"remaining_allocatable_bytes", remaining_allocatable_bytes}
            });
        }
    );
}
