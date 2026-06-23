#include "routes_admin_user_lifecycle.h"

#include "httplib.h"
#include "audit_log.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

} // namespace

void register_admin_user_lifecycle_routes(
    httplib::Server& srv,
    const AdminUserLifecycleRoutesContext& ctx
) {
    auto context_ok = [&]() -> bool {
        return ctx.users &&
               ctx.require_admin &&
               ctx.require_same_origin &&
               ctx.admin_would_remove_last_enabled_admin &&
               ctx.revoke_devices_for_fingerprint &&
               ctx.now_iso_utc &&
               ctx.audit_append;
    };

    auto reply_json_ctx = [&](httplib::Response& res, int status, const std::string& body) {
        if (ctx.reply_json) ctx.reply_json(res, status, body);
        else fallback_reply_json(res, status, body);
    };

    srv.Post("/api/v4/admin/users/disable",
        [ctx, context_ok, reply_json_ctx](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(res, status, body.dump());
            };

            if (!context_ok()) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user lifecycle route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!ctx.require_admin(req, res, &actor_fp)) return;
            if (!ctx.require_same_origin(req, res)) return;

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string fp = j.value("fingerprint", "");
            if (fp.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint"}});
                return;
            }
            if (!ctx.users->exists(fp)) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "user not found"}});
                return;
            }

            if (fp == actor_fp) {
                reply(400, json{
                    {"ok", false},
                    {"error", "self_disable"},
                    {"message", "Cannot disable your own admin account."}
                });
                return;
            }

            if (ctx.admin_would_remove_last_enabled_admin(fp)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "last_admin"},
                    {"message", "Cannot disable the last enabled admin."}
                });
                return;
            }

            if (!ctx.users->set_status(fp, "disabled")) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "set_status failed"}});
                return;
            }

            ctx.revoke_devices_for_fingerprint(fp);

            const bool saved = ctx.users->save(ctx.users_path);

            {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_disabled";
                ev.outcome = saved ? "ok" : "fail";
                ev.f["fingerprint"] = fp;
                ev.f["ts"] = ctx.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                ctx.audit_append(ev);
            }

            if (!saved) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "users save failed"}});
                return;
            }

            reply(200, json{{"ok", true}});
        }
    );

    srv.Post("/api/v4/admin/users/delete",
        [ctx, context_ok, reply_json_ctx](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(res, status, body.dump());
            };

            if (!context_ok()) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user lifecycle route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!ctx.require_admin(req, res, &actor_fp)) return;
            if (!ctx.require_same_origin(req, res)) return;

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string fp = j.value("fingerprint", "");
            if (fp.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint"}});
                return;
            }

            if (fp == actor_fp) {
                {
                    pqnas::AuditEvent ev;
                    ev.event = "admin.self_lockout_blocked";
                    ev.outcome = "fail";
                    ev.f["action"] = "delete";
                    ev.f["fingerprint"] = fp;
                    ev.f["ts"] = ctx.now_iso_utc();
                    ev.f["actor_fp"] = actor_fp;
                    ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                    ctx.audit_append(ev);
                }

                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "refusing to delete your own admin entry (prevents lockout)"}
                });
                return;
            }

            if (!ctx.users->exists(fp)) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "user not found"}});
                return;
            }

            if (ctx.users->is_admin_enabled(fp)) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "refusing to delete an enabled admin"}
                });
                return;
            }

            const bool ok_del = ctx.users->erase(fp);
            const bool ok_save = ok_del ? ctx.users->save(ctx.users_path) : false;

            {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_deleted";
                ev.outcome = (ok_del && ok_save) ? "ok" : "fail";
                ev.f["fingerprint"] = fp;
                ev.f["ts"] = ctx.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                ctx.audit_append(ev);
            }

            if (!ok_del) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "delete failed"}});
                return;
            }
            if (!ok_save) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "users save failed"}});
                return;
            }

            reply(200, json{{"ok", true}});
        }
    );
}
