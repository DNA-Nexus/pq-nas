#include "backups/system_backup_routes.h"

#include <algorithm>
#include <string>

#include <nlohmann/json.hpp>

namespace pqnas::backups {
namespace {

using json = nlohmann::json;

void reply_json_local(const SystemBackupRoutesDeps& deps,
                      httplib::Response& res,
                      int code,
                      const json& body) {
    if (deps.reply_json) {
        deps.reply_json(res, code, body.dump());
        return;
    }

    res.status = code;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

bool require_admin_local(const SystemBackupRoutesDeps& deps,
                         const httplib::Request& req,
                         httplib::Response& res) {
    if (!deps.users || !deps.cookie_key || !deps.require_user_auth_users_actor) {
        reply_json_local(deps, res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "system backup route dependencies missing"}
        });
        return false;
    }

    std::string actor_fp;
    std::string actor_role;
    if (!deps.require_user_auth_users_actor(
            req,
            res,
            deps.cookie_key,
            deps.users,
            &actor_fp,
            &actor_role)) {
        return false;
    }

    if (actor_role != "admin") {
        reply_json_local(deps, res, 403, {
            {"ok", false},
            {"error", "forbidden"},
            {"message", "admin role required"}
        });
        return false;
    }

    return true;
}

int parse_limit_local(const httplib::Request& req, int fallback) {
    if (!req.has_param("limit")) return fallback;

    try {
        return std::clamp(std::stoi(req.get_param_value("limit")), 1, 500);
    } catch (...) {
        return fallback;
    }
}

json parse_json_body_local(const httplib::Request& req) {
    if (req.body.empty()) return json::object();
    json j = json::parse(req.body, nullptr, false);
    return j.is_object() ? j : json::object();
}

} // namespace

void register_system_backup_routes(httplib::Server& srv, const SystemBackupRoutesDeps& deps) {
    srv.Get("/api/v4/admin/system-backups/status",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin_local(deps, req, res)) return;

            if (!deps.worker) {
                reply_json_local(deps, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "system backup worker missing"}
                });
                return;
            }

            reply_json_local(deps, res, 200, deps.worker->status_json());
        });

    srv.Get("/api/v4/admin/system-backups/list",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin_local(deps, req, res)) return;

            if (!deps.worker) {
                reply_json_local(deps, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "system backup worker missing"}
                });
                return;
            }

            reply_json_local(deps, res, 200, deps.worker->list_backups_json(parse_limit_local(req, 100)));
        });

    srv.Post("/api/v4/admin/system-backups/run",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin_local(deps, req, res)) return;

            if (!deps.worker) {
                reply_json_local(deps, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "system backup worker missing"}
                });
                return;
            }

            const json body = parse_json_body_local(req);
            const std::string tier = body.value("tier", "manual");
            const std::string reason = body.value("reason", "manual");

            const auto result = deps.worker->run_now(tier, reason);
            reply_json_local(deps, res, result.ok ? 200 : 500, system_backup_run_result_json(result));
        });

    srv.Post("/api/v4/admin/system-backups/prune",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin_local(deps, req, res)) return;

            if (!deps.worker) {
                reply_json_local(deps, res, 500, {
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "system backup worker missing"}
                });
                return;
            }

            const auto result = deps.worker->prune_now();
            reply_json_local(deps, res, result.ok ? 200 : 500, system_backup_run_result_json(result));
        });
}

} // namespace pqnas::backups
