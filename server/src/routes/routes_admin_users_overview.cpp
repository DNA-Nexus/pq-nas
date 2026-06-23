#include "routes_admin_users_overview.h"

#include "httplib.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminUsersOverviewRoutesContext& c) {
    return c.users &&
           c.require_admin_auth &&
           c.reply_json &&
           c.user_dir_for_fp &&
           c.dir_size_bytes_best_effort;
}

void reply_json_fallback(
    const AdminUsersOverviewRoutesContext& c,
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

} // namespace

void register_admin_users_overview_routes(
    httplib::Server& srv,
    const AdminUsersOverviewRoutesContext& ctx
) {
    const AdminUsersOverviewRoutesContext c = ctx;

    srv.Get("/api/v4/admin/users",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_json_fallback(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin users overview route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_auth(req, res, &actor_fp)) return;

            if (!c.users->load(c.users_path)) {
                reply_json_fallback(c, res, 500, json{
                    {"ok", false},
                    {"error", "users_reload_failed"},
                    {"message", "failed to reload users"}
                });
                return;
            }

            res.set_header("Cache-Control", "no-store");

            json out;
            out["ok"] = true;
            out["actor_fp"] = actor_fp;
            out["users"] = json::array();

            for (auto& kv : c.users->snapshot()) {
                auto& u = kv.second;

                std::uint64_t used_bytes = 0;
                if (u.storage_state == "allocated") {
                    const std::filesystem::path abs = c.user_dir_for_fp(u.fingerprint);
                    used_bytes = c.dir_size_bytes_best_effort(abs);
                }

                out["users"].push_back({
                    {"fingerprint", u.fingerprint},
                    {"name", u.name},
                    {"role", u.role},
                    {"status", u.status},
                    {"added_at", u.added_at},
                    {"last_seen", u.last_seen},
                    {"notes", u.notes},

                    {"group", u.group},
                    {"email", u.email},
                    {"address", u.address},
                    {"avatar_url", u.avatar_url},

                    {"storage_state", u.storage_state},
                    {"quota_bytes", u.quota_bytes},
                    {"root_rel", u.root_rel},
                    {"storage_set_at", u.storage_set_at},
                    {"storage_set_by", u.storage_set_by},

                    {"storage_pool_id", u.storage_pool_id},
                    {"pool_id", u.storage_pool_id.empty() ? "default" : u.storage_pool_id},

                    {"storage_used_bytes", used_bytes}
                });
            }

            reply_json_fallback(c, res, 200, out);
        }
    );
}
