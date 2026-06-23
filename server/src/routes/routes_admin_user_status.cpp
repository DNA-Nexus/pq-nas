#include "routes_admin_user_status.h"

#include "httplib.h"
#include "audit_log.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

bool context_ok(const AdminUserStatusRoutesContext& c) {
    return c.users &&
           c.require_admin_auth &&
           c.require_same_origin &&
           c.reply_json &&
           c.admin_would_remove_last_enabled_admin &&
           c.invalidate_opaque_enrollment_tokens_for_revoke &&
           c.revoke_devices_for_fingerprint &&
           c.now_iso_utc &&
           c.audit_append;
}

void reply_json_local(
    const AdminUserStatusRoutesContext& c,
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

} // namespace

void register_admin_user_status_routes(
    httplib::Server& srv,
    const AdminUserStatusRoutesContext& ctx
) {
    const AdminUserStatusRoutesContext c = ctx;

    srv.Post("/api/v4/admin/users/status",
        [c](const httplib::Request& req, httplib::Response& res) {
            if (!context_ok(c)) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user status route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_auth(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

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
            const std::string status = j.value("status", "");

            if (fp.empty()) {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint"}
                });
                return;
            }

            if (status != "enabled" && status != "disabled" && status != "revoked") {
                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "invalid status"}
                });
                return;
            }

            if (fp == actor_fp && status != "enabled") {
                pqnas::AuditEvent ev;
                ev.event = "admin.self_lockout_blocked";
                ev.outcome = "fail";
                ev.f["action"] = "status";
                ev.f["fingerprint"] = fp;
                ev.f["requested_status"] = status;
                ev.f["ts"] = c.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req_ip(req);
                c.audit_append(ev);

                reply_json_local(c, res, 400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "refusing to change your own status (prevents admin lockout)"}
                });
                return;
            }

            if ((status == "disabled" || status == "revoked") &&
                c.admin_would_remove_last_enabled_admin(fp)) {
                res.status = 400;
                res.set_content(
                    "{\"ok\":false,\"error\":\"last_admin\",\"message\":\"Cannot disable or revoke the last enabled admin.\"}",
                    "application/json"
                );
                return;
            }

            if (!c.users->exists(fp)) {
                reply_json_local(c, res, 404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "user not found"}
                });
                return;
            }

            if (status == "revoked") {
                std::size_t opaque_tokens_invalidated = 0;
                std::string opaque_token_invalidate_err;

                if (!c.invalidate_opaque_enrollment_tokens_for_revoke(
                        fp,
                        &opaque_tokens_invalidated,
                        &opaque_token_invalidate_err)) {
                    pqnas::AuditEvent ev;
                    ev.event = "opaque.enrollment_tokens_invalidate_on_user_revoke";
                    ev.outcome = "fail";
                    ev.f["fingerprint"] = fp;
                    ev.f["reason"] = opaque_token_invalidate_err;
                    ev.f["ts"] = c.now_iso_utc();
                    ev.f["actor_fp"] = actor_fp;
                    ev.f["ip"] = req_ip(req);
                    c.audit_append(ev);

                    reply_json_local(c, res, 500, json{
                        {"ok", false},
                        {"error", "server_error"},
                        {"message", "opaque enrollment token invalidation failed"},
                        {"detail", opaque_token_invalidate_err}
                    });
                    return;
                }

                pqnas::AuditEvent ev;
                ev.event = "opaque.enrollment_tokens_invalidate_on_user_revoke";
                ev.outcome = "ok";
                ev.f["fingerprint"] = fp;
                ev.f["invalidated"] = std::to_string(opaque_tokens_invalidated);
                ev.f["ts"] = c.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req_ip(req);
                c.audit_append(ev);
            }

            const bool ok_set = c.users->set_status(fp, status);
            if (status == "disabled" || status == "revoked") {
                c.revoke_devices_for_fingerprint(fp);
            }

            const bool ok_save = ok_set ? c.users->save(c.users_path) : false;

            {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_status_set";
                ev.outcome = (ok_set && ok_save) ? "ok" : "fail";
                ev.f["fingerprint"] = fp;
                ev.f["status"] = status;
                ev.f["ts"] = c.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req_ip(req);
                c.audit_append(ev);
            }

            if (!ok_set) {
                reply_json_local(c, res, 500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "set_status failed"}
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

            reply_json_local(c, res, 200, json{{"ok", true}});
        }
    );
}
