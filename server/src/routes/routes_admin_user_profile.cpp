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

            auto existing_user_for_immutable_fp = c.users->get(fp);
            if (!existing_user_for_immutable_fp.has_value()) {
                pqnas::AuditEvent ev;
                ev.event = "admin.users.upsert_reject_unknown_fingerprint";
                ev.outcome = "deny";
                ev.f["fingerprint"] = fp;
                ev.f["reason"] = "fingerprint_is_immutable_and_must_already_exist";
                ev.f["ts"] = c.now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);

                reply(400, json{
                    {"ok", false},
                    {"error", "fingerprint_immutable"},
                    {"message", "fingerprint is immutable; admin users upsert can only update an existing user profile"}
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

            if (fp.empty()) {
                reply(400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing fingerprint"}});
                return;
            }

            const std::string now_iso = c.now_iso_utc();

            pqnas::UserRec u{};
            bool existed = false;

            if (auto cur = c.users->get(fp); cur.has_value()) {
                existed = true;
                u = *cur;
            } else {
                u.fingerprint = fp;
                u.added_at = now_iso;
                u.last_seen = "";
                u.status = "disabled";
                u.role = "user";
                u.name = "";
                u.notes = "";
                u.avatar_url = "";
            }

            const bool is_self = (!actor_fp.empty() && fp == actor_fp);

            u.name = name;
            u.notes = notes;
            u.email = email;
            u.address = address;
            u.group = group;
            u.avatar_url = avatar_url;

            if (!is_self) {
                u.role = role;
            }

            const bool ok_upsert = c.users->upsert(u);
            const bool ok_save = ok_upsert ? c.users->save(c.users_path) : false;

            {
                pqnas::AuditEvent ev;
                ev.event = "admin.user_upsert";
                ev.outcome = (ok_upsert && ok_save) ? "ok" : "fail";
                ev.f["fingerprint"] = fp;
                ev.f["existed"] = existed ? "true" : "false";
                ev.f["role_requested"] = role;
                ev.f["role_effective"] = u.role;
                if (is_self && role != u.role) ev.f["self_role_change_blocked"] = "true";
                if (!name.empty()) ev.f["name"] = pqnas::shorten(name, 80);
                if (!notes.empty()) ev.f["notes"] = pqnas::shorten(notes, 120);
                ev.f["ts"] = now_iso;
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                c.audit_append(ev);
            }

            if (!ok_upsert) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "upsert failed"}});
                return;
            }
            if (!ok_save) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "users save failed"}});
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

            if (!c.users->set_status(fp, "enabled")) {
                reply(500, json{{"ok", false}, {"error", "server_error"}, {"message", "set_status failed"}});
                return;
            }

            c.users->set_role(fp, role);
            if (!name.empty() || !notes.empty()) {
                c.users->set_name_notes(fp, name, notes);
            }

            const bool saved = c.users->save(c.users_path);

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
