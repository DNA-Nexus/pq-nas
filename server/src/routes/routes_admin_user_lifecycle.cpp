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

            // Security: disabling the user and advancing the Storage
            // projection revision are one persisted registry mutation.
            const bool saved =
                ctx.users->set_status_persisted(
                    fp,
                    "disabled",
                    ctx.users_path,
                    nullptr,
                    nullptr
                );

            if (saved) {
                ctx.revoke_devices_for_fingerprint(fp);
            }

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

            bool erased = false;
            bool placement_blocked = false;
            bool cluster_revoke_pending = false;
            std::string delete_error;

            // Security: Director deletion must serialize the confirmed
            // projection-state check with reconciliation and keep that boundary
            // held through the durable users.json erase. Standalone keeps the
            // ordinary persisted unplaced-user path.
            bool ok_del = false;

            if (ctx.erase_user_persisted_cluster_safe) {
                ok_del =
                    ctx.erase_user_persisted_cluster_safe(
                        fp,
                        &erased,
                        &placement_blocked,
                        &cluster_revoke_pending,
                        &delete_error
                    );
            } else {
                ok_del =
                    ctx.users->erase_persisted_if_unplaced(
                        fp,
                        ctx.users_path,
                        &erased,
                        &placement_blocked,
                        &delete_error
                    );
            }

            {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_deleted";
                ev.outcome =
                    (ok_del && erased)
                        ? "ok"
                        : "fail";
                ev.f["fingerprint"] = fp;
                ev.f["ts"] = ctx.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] =
                    req.remote_addr.empty()
                        ? "?"
                        : req.remote_addr;
                ctx.audit_append(ev);
            }

            if (!ok_del) {
                if (delete_error == "user not found") {
                    reply(
                        404,
                        json{
                            {"ok", false},
                            {"error", "not_found"},
                            {"message", "user not found"}
                        }
                    );
                    return;
                }

                reply(
                    500,
                    json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "user delete persistence failed"}
                    }
                );
                return;
            }

            if (placement_blocked) {
                reply(
                    409,
                    json{
                        {"ok", false},
                        {"error", "cluster_revoke_required"},
                        {
                            "message",
                            "placed user must be revoked from Storage before deletion"
                        }
                    }
                );
                return;
            }

            if (cluster_revoke_pending) {
                reply(
                    409,
                    json{
                        {"ok", false},
                        {"error", "cluster_revoke_pending"},
                        {
                            "message",
                            "Storage revoke reconciliation is still pending"
                        }
                    }
                );
                return;
            }

            if (!erased) {
                reply(
                    500,
                    json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "delete failed"}
                    }
                );
                return;
            }

            reply(200, json{{"ok", true}});
        }
    );
}
