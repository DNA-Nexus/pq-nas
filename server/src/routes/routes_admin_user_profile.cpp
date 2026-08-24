#include "routes_admin_user_profile.h"

#include "httplib.h"
#include "audit_log.h"
#include "audit_fields.h"
#include "users_registry.h"

#include <nlohmann/json.hpp>

#include <string>

using json = nlohmann::json;

namespace {

void fallback_reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "application/json; charset=utf-8");
}

void reply_json_ctx(
    const AdminUserProfileRoutesContext& c,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (c.reply_json) c.reply_json(res, status, body.dump());
    else fallback_reply_json(res, status, body.dump());
}

bool context_ok(const AdminUserProfileRoutesContext& c) {
    return c.users &&
           c.require_admin_cookie &&
           c.require_admin_auth &&
           c.require_same_origin &&
           c.reply_json &&
           c.now_iso_utc &&
           c.audit_append;
}

} // namespace

void register_admin_user_profile_routes(
    httplib::Server& srv,
    const AdminUserProfileRoutesContext& ctx
) {
    const AdminUserProfileRoutesContext c = ctx;

    srv.Post("/api/v4/admin/users/upsert",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user profile route context incomplete"}
                });
                return;
            }

            std::string actor_fp;
            if (!c.require_admin_cookie(req, res, &actor_fp)) return;
            if (!c.require_same_origin(req, res)) return;

            json j;
            try {
                j = json::parse(req.body);
            } catch (...) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string fp = j.value("fingerprint", "");

            if (fp.empty()) {
                reply(400, json{
                    {"ok", false},
                    {"error", "bad_request"},
                    {"message", "missing fingerprint"}
                });
                return;
            }

            const std::string name = j.value("name", "");
            const std::string role = j.value("role", "user");
            const std::string notes = j.value("notes", "");
            const std::string email = j.value("email", "");
            const std::string avatar_url = j.value("avatar_url", "");
            const std::string group = j.value("group", "");
            const std::string address = j.value("address", "");

            const std::string now_iso =
                c.now_iso_utc();

            const bool is_self =
                !actor_fp.empty() &&
                fp == actor_fp;

            pqnas::UserProfileUpdate update;
            update.name = name;
            update.notes = notes;
            update.email = email;
            update.address = address;
            update.group = group;
            update.avatar_url = avatar_url;
            update.update_role = !is_self;
            update.role = role;

            bool profile_changed = false;
            pqnas::UserRec updated_user;
            std::string profile_error;

            // Security: profile writes are applied to the CURRENT registry
            // record while its mutation lock is held. A stale request snapshot
            // can therefore never resurrect older placement/projection state.
            const bool ok_profile =
                c.users->set_profile_persisted(
                    fp,
                    update,
                    c.users_path,
                    &profile_changed,
                    &updated_user,
                    &profile_error
                );

            if (!ok_profile &&
                profile_error == "user not found") {
                pqnas::AuditEvent ev;
                ev.event =
                    "admin.users.upsert_reject_unknown_fingerprint";
                ev.outcome = "deny";
                ev.f["fingerprint"] = fp;
                ev.f["reason"] =
                    "fingerprint_is_immutable_and_must_already_exist";
                ev.f["ts"] = now_iso;
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] =
                    req.remote_addr.empty()
                        ? "?"
                        : req.remote_addr;
                c.audit_append(ev);

                reply(400, json{
                    {"ok", false},
                    {"error", "fingerprint_immutable"},
                    {
                        "message",
                        "fingerprint is immutable; admin users upsert can only update an existing user profile"
                    }
                });
                return;
            }

            {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_upsert";
                ev.outcome =
                    ok_profile
                        ? "ok"
                        : "fail";

                ev.f["fingerprint"] = fp;
                ev.f["existed"] =
                    ok_profile
                        ? "true"
                        : "unknown";
                ev.f["role_requested"] = role;

                if (ok_profile) {
                    ev.f["role_effective"] =
                        updated_user.role;

                    ev.f["profile_changed"] =
                        profile_changed
                            ? "true"
                            : "false";

                    if (is_self &&
                        role != updated_user.role) {
                        ev.f["self_role_change_blocked"] =
                            "true";
                    }
                }

                if (!name.empty()) {
                    ev.f["name"] =
                        pqnas::shorten(name, 80);
                }

                if (!notes.empty()) {
                    ev.f["notes"] =
                        pqnas::shorten(notes, 120);
                }

                ev.f["ts"] = now_iso;
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] =
                    req.remote_addr.empty()
                        ? "?"
                        : req.remote_addr;

                c.audit_append(ev);
            }

            if (!ok_profile) {
                // Security: do not expose users.json paths, errno values or
                // persistence internals through this admin API.
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "user profile update failed"}
                });
                return;
            }

            reply(200, json{{"ok", true}});
        }
    );

    srv.Post("/api/v4/admin/users/enable",
        [c](const httplib::Request& req, httplib::Response& res) {
            auto reply = [&](int status, const json& body) {
                reply_json_ctx(c, res, status, body);
            };

            if (!context_ok(c)) {
                reply(500, json{
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "admin user profile route context incomplete"}
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
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid json"}});
                return;
            }

            const std::string fp = j.value("fingerprint", "");
            const std::string role = j.value("role", "user");
            const std::string name = j.value("name", "");
            const std::string notes = j.value("notes", "");

            if (fp.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint"}});
                return;
            }
            if (!c.users->exists(fp)) {
                reply(404, json{{"ok", false}, {"error", "not_found"}, {"message", "user not found"}});
                return;
            }

            auto user =
                c.users->get(fp);

            if (!user.has_value()) {
                reply(404, json{
                    {"ok", false},
                    {"error", "not_found"},
                    {"message", "user not found"}
                });
                return;
            }

            pqnas::UserRec updated_user =
                *user;

            updated_user.status =
                "enabled";

            updated_user.role =
                role;

            if (!name.empty() || !notes.empty()) {
                updated_user.name =
                    name;
                updated_user.notes =
                    notes;
            }

            // Security: status and its Storage projection revision must be
            // committed atomically. A failed users.json write must not leave
            // an enabled user visible only in memory or without a revision bump.
            const bool saved =
                c.users->upsert_persisted(
                    updated_user,
                    c.users_path,
                    nullptr,
                    nullptr
                );

            {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_enabled";
                ev.outcome = saved ? "ok" : "fail";
                ev.f["fingerprint"] = fp;
                ev.f["role"] = role;
                ev.f["ts"] = c.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;

                if (!name.empty()) ev.f["name"] = pqnas::shorten(name, 80);
                if (!notes.empty()) ev.f["notes"] = pqnas::shorten(notes, 120);
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);
            }

            if (!saved) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "users save failed"}});
                return;
            }

            reply(200, json{{"ok", true}});
        }
    );
}
