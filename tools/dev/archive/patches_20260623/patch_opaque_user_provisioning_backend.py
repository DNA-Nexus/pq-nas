#!/usr/bin/env python3
from pathlib import Path
import sys

path = Path("server/src/routes_v5.cc")
if not path.exists():
    print(f"ERROR: missing {path}", file=sys.stderr)
    sys.exit(1)

text = path.read_text()

route_marker = "POST /api/admin/users/opaque-create"
if route_marker not in text:
    insert_anchor = "\n\n    // ---- POST/GET /api/v5/session ----"
    if insert_anchor not in text:
        print("ERROR: /api/v5/session insertion anchor not found", file=sys.stderr)
        sys.exit(1)

    opaque_create_route = r'''
    // ---- POST /api/admin/users/opaque-create ----
    //
    // Admin-only OPAQUE user provisioning.
    //
    // This creates the DNA-Nexus / PQ-NAS user identity and returns recovery
    // words once, but it deliberately does NOT accept or store a plaintext
    // password. OPAQUE credential enrollment must be completed through the
    // OPAQUE registration endpoints so the browser never sends the password
    // to the server.
    srv.Post("/api/admin/users/opaque-create", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "opaque_auth_disabled"},
                {"mode", routes_v5_auth_mode()}
            }.dump());
            return;
        }

        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "auth_cookie_checker_not_configured"}
            }.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.user_create", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        if (routes_v5_has_forbidden_password_fallback_field(j)) {
            routes_v5_audit_password(ctx, req, "opaque.user_create", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string name =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "name"));
        const bool include_public_key = j.value("include_public_key", false);

        std::uint64_t requested_quota_bytes = 0;
        if (j.contains("quota_bytes") && !j["quota_bytes"].is_null()) {
            if (!j["quota_bytes"].is_number_unsigned()) {
                reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_quota_bytes"}}.dump());
                return;
            }
            requested_quota_bytes = j["quota_bytes"].get<std::uint64_t>();
        }

        std::string role =
            routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "role")));
        if (role.empty()) role = "user";

        // Default to disabled because the user cannot log in until an OPAQUE
        // credential has been enrolled. UsersRegistry currently supports the
        // existing enabled/disabled/revoked status model; OPAQUE enrollment
        // finish can promote the user to enabled after the credential is stored.
        std::string status =
            routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "status")));
        if (status.empty()) status = "disabled";

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (role != "user" && role != "admin") {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_role"}}.dump());
            return;
        }

        if (status != "enabled" && status != "disabled" && status != "revoked") {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_status"}}.dump());
            return;
        }

        if (!ctx.users || !ctx.users_path || ctx.users_path->empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        {
            const auto snap = ctx.users->snapshot();
            for (const auto& kv : snap) {
                const pqnas::UserRec& existing = kv.second;
                if (!existing.email.empty() &&
                    pqnas::OpaqueCredentials::normalize_login(existing.email) == login) {
                    routes_v5_audit_password(ctx, req, "opaque.user_create", "deny", login, existing.fingerprint, "login_already_exists");
                    reply_json(res, 409, json{
                        {"ok", false},
                        {"error", "login_already_exists"},
                        {"fingerprint", existing.fingerprint}
                    }.dump());
                    return;
                }
            }
        }

        pqnas::GeneratedDnaIdentity ident;
        std::string gen_error;
        if (!pqnas::generate_dna_identity(ident, gen_error)) {
            routes_v5_secure_clear_string(ident.recovery_words);
            routes_v5_audit_password(ctx, req, "opaque.user_create", "deny", login, "", "identity_generation_failed");
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "identity_generation_failed"},
                {"message", gen_error}
            }.dump());
            return;
        }

        if (ctx.users->get(ident.fingerprint_hex).has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.user_create", "deny", login, ident.fingerprint_hex, "fingerprint_collision");
            routes_v5_secure_clear_string(ident.recovery_words);
            reply_json(res, 409, json{{"ok", false}, {"error", "fingerprint_already_exists"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        pqnas::UserRec u;
        u.fingerprint = ident.fingerprint_hex;
        u.name = name.empty() ? login : name;
        u.role = role;
        u.status = status;
        u.added_at = now_iso;
        u.last_seen = "";
        u.notes = "Created by OPAQUE provisioning; awaiting OPAQUE credential enrollment";
        u.group = "";
        u.email = login;
        u.address = "";
        u.avatar_url = "";
        u.storage_state = "unallocated";
        u.quota_bytes = requested_quota_bytes;
        u.root_rel = "";
        u.storage_pool_id = "";
        u.storage_set_at = "";
        u.storage_set_by = "";

        if (!ctx.users->upsert(u) || !ctx.users->save(*ctx.users_path)) {
            routes_v5_audit_password(ctx, req, "opaque.user_create", "deny", login, ident.fingerprint_hex, "users_save_failed");
            routes_v5_secure_clear_string(ident.recovery_words);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.user_create", "ok", login, ident.fingerprint_hex, "");

        json out = {
            {"ok", true},
            {"login", login},
            {"fingerprint", ident.fingerprint_hex},
            {"role", role},
            {"status", status},
            {"quota_bytes", requested_quota_bytes},
            {"opaque_enrollment_required", true},
            {"ready_for_login", false},
            {"setup_state", status == "enabled" ? "credential_required_enabled_user" : "credential_required_disabled_user"},
            {"recovery_words_shown_once", true},
            {"warning", "Recovery words are shown once and are not stored by the server. OPAQUE credential enrollment is still required before login works. Keep the user disabled until enrollment finish enables it."}
        };

        if (include_public_key) {
            out["public_key_b64"] = ident.public_key_b64;
        }

        std::string response_body = out.dump();
        std::string recovery_words_json = json(ident.recovery_words).dump();

        if (!routes_v5_append_json_member_to_object(
                response_body,
                std::string("\"recovery_words\":") + recovery_words_json)) {
            routes_v5_secure_clear_string(recovery_words_json);
            routes_v5_secure_clear_string(ident.recovery_words);
            routes_v5_secure_clear_string(response_body);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "response_build_failed"}}.dump());
            return;
        }

        routes_v5_secure_clear_string(recovery_words_json);
        routes_v5_secure_clear_string(ident.recovery_words);
        reply_json(res, 200, response_body);
        routes_v5_secure_clear_string(response_body);
    });
'''

    text = text.replace(insert_anchor, "\n\n" + opaque_create_route + insert_anchor, 1)
    print("inserted /api/admin/users/opaque-create")
else:
    print("unchanged: /api/admin/users/opaque-create already present")

old_finish_tail = '''        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "opaque_credentials_save_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.registration_finish", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"opaque_suite", opaque_suite},
            {"enabled", rec.enabled},
            {"temporary", rec.temporary},
            {"ready_for_login", false},
            {"warning", "OPAQUE enrollment was completed and stored, but OPAQUE login/session minting is still intentionally disabled."}
        }.dump());
'''

new_finish_tail = '''        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "opaque_credentials_save_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_save_failed"}}.dump());
            return;
        }

        std::string final_user_status = user->status;
        bool user_enabled_by_enrollment = false;
        const bool enable_user = j.value("enable_user", false);

        if (enable_user) {
            if (!ctx.users_path || ctx.users_path->empty()) {
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_path_not_configured"}}.dump());
                return;
            }

            pqnas::UserRec updated_user = *user;
            updated_user.status = "enabled";
            final_user_status = updated_user.status;
            user_enabled_by_enrollment = (user->status != "enabled");

            if (!ctx.users->upsert(updated_user) || !ctx.users->save(*ctx.users_path)) {
                routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "user_enable_failed");
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "user_enable_failed"}}.dump());
                return;
            }
        }

        const bool ready_for_login = rec.enabled && final_user_status == "enabled";

        routes_v5_audit_password(ctx, req, "opaque.registration_finish", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"opaque_suite", opaque_suite},
            {"enabled", rec.enabled},
            {"temporary", rec.temporary},
            {"user_status", final_user_status},
            {"user_enabled_by_enrollment", user_enabled_by_enrollment},
            {"ready_for_login", ready_for_login}
        }.dump());
'''

if old_finish_tail in text:
    text = text.replace(old_finish_tail, new_finish_tail, 1)
    print("patched opaque registration finish ready_for_login/enable_user")
elif "user_enabled_by_enrollment" in text:
    print("unchanged: opaque registration finish already patched")
else:
    print("ERROR: opaque registration finish tail anchor not found", file=sys.stderr)
    sys.exit(1)

path.write_text(text)
print(f"patched: {path}")
