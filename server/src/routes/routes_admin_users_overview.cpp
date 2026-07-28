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

void handle_admin_users_overview(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminUsersOverviewRoutesContext& ctx
) {
    if (!context_ok(ctx)) {
        reply_json_fallback(
            ctx,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "admin users overview route context incomplete"
                }
            }
        );
        return;
    }

    std::string actor_fp;

    if (!ctx.require_admin_auth(
            req,
            res,
            &actor_fp)) {
        return;
    }

    if (!ctx.users->load(ctx.users_path)) {
        reply_json_fallback(
            ctx,
            res,
            500,
            json{
                {"ok", false},
                {"error", "users_reload_failed"},
                {"message", "failed to reload users"}
            }
        );
        return;
    }

    res.set_header(
        "Cache-Control",
        "no-store"
    );

    json out;
    out["ok"] = true;
    out["actor_fp"] = actor_fp;
    out["users"] = json::array();

    for (auto& entry : ctx.users->snapshot()) {
        auto& user = entry.second;

        std::uint64_t used_bytes = 0;

        if (user.storage_state == "allocated") {
            const std::filesystem::path absolute_path =
                ctx.user_dir_for_fp(
                    user.fingerprint
                );

            used_bytes =
                ctx.dir_size_bytes_best_effort(
                    absolute_path
                );
        }

        out["users"].push_back({
            {"fingerprint", user.fingerprint},
            {"name", user.name},
            {"role", user.role},
            {"status", user.status},
            {"added_at", user.added_at},
            {"last_seen", user.last_seen},
            {"notes", user.notes},

            {"group", user.group},
            {"email", user.email},
            {"address", user.address},
            {"avatar_url", user.avatar_url},

            {"storage_state", user.storage_state},
            {"quota_bytes", user.quota_bytes},
            {"root_rel", user.root_rel},
            {"storage_set_at", user.storage_set_at},
            {"storage_set_by", user.storage_set_by},

            {"storage_pool_id", user.storage_pool_id},
            {
                "pool_id",
                user.storage_pool_id.empty()
                    ? "default"
                    : user.storage_pool_id
            },

            {"storage_used_bytes", used_bytes},

            // Empty means the existing local/standalone placement.
            {"home_node", user.home_node}
        });
    }

    reply_json_fallback(
        ctx,
        res,
        200,
        out
    );
}

void register_admin_users_overview_routes(
    httplib::Server& srv,
    const AdminUsersOverviewRoutesContext& ctx
) {
    const AdminUsersOverviewRoutesContext captured =
        ctx;

    srv.Get(
        "/api/v4/admin/users",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_users_overview(
                req,
                res,
                captured
            );
        }
    );
}
