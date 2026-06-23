#!/usr/bin/env python3
from pathlib import Path
import re
import sys

routes = Path("server/src/routes_v5.cc")
js_path = Path("server/src/static/admin_approvals.js")
html_path = Path("server/src/static/admin_approvals.html")

for p in (routes, js_path):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

s = routes.read_text()

def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

endpoint_marker = 'srv.Post("/api/admin/auth/opaque/force-reset"'
if endpoint_marker not in s:
    insert_anchor = '''    // ---- GET /api/admin/auth/opaque/onboarding/status ----
'''
    if insert_anchor not in s:
        die("onboarding status insertion anchor not found")

    endpoint = r'''
    // ---- POST /api/admin/auth/opaque/force-reset ----
    //
    // Admin-only atomic-ish force reset.
    //
    // This replaces the old frontend two-step flow:
    //   1) disable credential
    //   2) create reset token
    //
    // The server creates and persists the reset token before disabling the old
    // credential. That avoids the dangerous state where the old credential is
    // disabled but no reset token exists for the user.
    srv.Post("/api/admin/auth/opaque/force-reset", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_auth_disabled"}, {"mode", routes_v5_auth_mode()}}.dump());
            return;
        }

        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "auth_cookie_checker_not_configured"}}.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", "", actor_fp, "not_admin");
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
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));

        if (login.empty() && fingerprint.empty()) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing_login_or_fingerprint"}}.dump());
            return;
        }

        if (!login.empty() && (login.size() > 254 || routes_v5_has_control_chars(login))) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        std::optional<pqnas::UserRec> user;
        if (!fingerprint.empty()) {
            user = ctx.users->get(fingerprint);
            if (user.has_value() && login.empty()) {
                login = pqnas::OpaqueCredentials::normalize_login(user->email);
            }
        } else {
            user = routes_v5_find_user_by_login_local(ctx, login, &fingerprint);
        }

        if (!user.has_value() || login.empty() || fingerprint.empty()) {
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        if (user->status == "revoked") {
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "user_revoked");
            reply_json(res, 409, json{{"ok", false}, {"error", "user_revoked"}}.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const auto existing = creds.get(login);
        if (!existing.has_value() || existing->fingerprint != fingerprint) {
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "opaque_credential_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_credential_missing"}}.dump());
            return;
        }

        long ttl = 86400;
        if (j.contains("expires_in_seconds") && j["expires_in_seconds"].is_number_integer()) {
            ttl = j["expires_in_seconds"].get<long>();
        }
        if (ttl < 300) ttl = 300;
        if (ttl > 604800) ttl = 604800;

        const long now = routes_v5_now_epoch_safe(ctx);
        std::string token = ctx.random_b64url ? ctx.random_b64url(32) : std::string{};
        if (!routes_v5_is_safe_enrollment_token(token)) {
            token = routes_v5_random_hex_id_128() + routes_v5_random_hex_id_128();
        }
        if (!routes_v5_is_safe_enrollment_token(token)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "token_rng_failed"}}.dump());
            return;
        }

        const std::string token_hash = sha256_hex(token);
        const std::string enrollments_path = routes_v5_opaque_enrollments_path(ctx);

        {
            std::lock_guard<std::mutex> lock(routes_v5_opaque_enrollments_file_mu());

            std::string lerr;
            json doc = routes_v5_load_opaque_enrollments_no_lock(enrollments_path, &lerr);
            if (!lerr.empty()) {
                routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "opaque_enrollments_load_failed");
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_enrollments_load_failed"}, {"detail", lerr}}.dump());
                return;
            }

            routes_v5_prune_opaque_enrollments_doc(doc, now);

            routes_v5_invalidate_active_opaque_enrollment_tokens(
                doc,
                login,
                fingerprint,
                now,
                "replaced_by_force_reset");

            doc["tokens"].push_back(json{
                {"token_hash", token_hash},
                {"login", login},
                {"fingerprint", fingerprint},
                {"purpose", "reset_password"},
                {"created_by_fp", actor_fp},
                {"user_status_at_issue", user->status},
                {"created_at", now},
                {"expires_at", now + ttl},
                {"used_at", 0},
                {"enable_user_on_finish", true},
                {"force_reset", true}
            });

            std::string serr;
            if (!routes_v5_save_opaque_enrollments_no_lock(enrollments_path, doc, &serr)) {
                routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "opaque_enrollments_save_failed");
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_enrollments_save_failed"}, {"detail", serr}}.dump());
                return;
            }
        }

        pqnas::OpaqueCredentialRec rec = *existing;
        rec.enabled = false;
        rec.updated_at = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "opaque_credentials_save_failed_after_token_create");
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "opaque_credentials_save_failed_after_token_create"},
                {"note", "reset_token_was_created_but_old_credential_may_still_be_enabled"}
            }.dump());
            return;
        }

        const std::string setup_path = std::string("/static/opaque-enroll.html?token=") + token;

        std::string origin = req_header_or_empty(req, "Origin");
        if (origin.empty()) {
            const std::string host = req_header_or_empty(req, "Host");
            std::string proto = req_header_or_empty(req, "X-Forwarded-Proto");
            if (proto.empty()) proto = "https";
            if (!host.empty()) origin = proto + "://" + host;
        }

        const std::string setup_url = origin.empty() ? setup_path : (origin + setup_path);

        routes_v5_audit_password(ctx, req, "opaque.force_reset", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"purpose", "reset_password"},
            {"expires_at", now + ttl},
            {"enable_user_on_finish", true},
            {"credential_enabled", false},
            {"user_status", user->status},
            {"ready_for_login", false},
            {"token_shown_once", true},
            {"token", token},
            {"setup_path", setup_path},
            {"setup_url", setup_url}
        }.dump());
    });


'''
    s = s.replace(insert_anchor, endpoint + insert_anchor, 1)
    routes.write_text(s)
    print("patched: added /api/admin/auth/opaque/force-reset endpoint")
else:
    print("unchanged: force-reset endpoint already exists")

# ---------------------------------------------------------------------
# Frontend: replace two HTTP calls with one backend force-reset call.
# ---------------------------------------------------------------------
js = js_path.read_text()

old_force = '''async function forceOpaqueReset(row) {
    const ok = await openApprovalsConfirmModal({
        title: tr("admin.approvals.opaque.force_reset_title", null, "Pakota salasanan reset"),
        subtitle: row.login || "",
        rows: [
            { label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: row.name || row.login || "" },
            { label: "Login", value: row.login || "", mono: true },
            { label: tr("admin.approvals.fingerprint", null, "Fingerprint"), value: row.fingerprint || "", mono: true },
        ],
        note: tr("admin.approvals.opaque.force_reset_note", null, "Vanha OPAQUE-salasana lakkaa toimimasta heti. Käyttäjä pääsee sisään vasta uuden reset-linkin suorittamisen jälkeen."),
        confirmText: tr("admin.approvals.opaque.force_reset_confirm", null, "Pakota reset"),
        cancelText: tr("admin.approvals.cancel", null, "Cancel"),
        danger: true,
    });
    if (!ok) return;

    setMsg(tr("admin.approvals.opaque.force_resetting", null, "Pakotetaan reset…"));

    await apiPost("/api/admin/auth/opaque/credential/disable", {
        login: row.login,
        fingerprint: row.fingerprint
    });

    const token = await apiPost("/api/admin/auth/opaque/enrollment-token/create", {
        login: row.login,
        fingerprint: row.fingerprint,
        purpose: "reset_password",
        enable_user_on_finish: true,
        expires_in_seconds: 86400
    });

    await showSetupLinkModal(row, token);
    await refresh();
}
'''

new_force = '''async function forceOpaqueReset(row) {
    const ok = await openApprovalsConfirmModal({
        title: tr("admin.approvals.opaque.force_reset_title", null, "Pakota salasanan reset"),
        subtitle: row.login || "",
        rows: [
            { label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: row.name || row.login || "" },
            { label: "Login", value: row.login || "", mono: true },
            { label: tr("admin.approvals.fingerprint", null, "Fingerprint"), value: row.fingerprint || "", mono: true },
        ],
        note: tr("admin.approvals.opaque.force_reset_note", null, "Vanha OPAQUE-salasana lakkaa toimimasta heti. Käyttäjä pääsee sisään vasta uuden reset-linkin suorittamisen jälkeen."),
        confirmText: tr("admin.approvals.opaque.force_reset_confirm", null, "Pakota reset"),
        cancelText: tr("admin.approvals.cancel", null, "Cancel"),
        danger: true,
    });
    if (!ok) return;

    setMsg(tr("admin.approvals.opaque.force_resetting", null, "Pakotetaan reset…"));

    const token = await apiPost("/api/admin/auth/opaque/force-reset", {
        login: row.login,
        fingerprint: row.fingerprint,
        expires_in_seconds: 86400
    });

    await showSetupLinkModal(row, token);
    await refresh();
}
'''

if old_force in js:
    js = js.replace(old_force, new_force, 1)
    js_path.write_text(js)
    print("patched: admin_approvals forceOpaqueReset now uses force-reset endpoint")
elif '/api/admin/auth/opaque/force-reset' in js:
    print("unchanged: admin_approvals already uses force-reset endpoint")
else:
    die("forceOpaqueReset exact block not found")

# Cache buster.
if html_path.exists():
    h = html_path.read_text()
    h2 = re.sub(
        r'admin_approvals\.js\?v=[^"]+',
        'admin_approvals.js?v=20260613-opaque-force-reset-endpoint-1',
        h,
        count=1
    )
    if h2 != h:
        html_path.write_text(h2)
        print("patched: admin_approvals.js cache buster")
    else:
        print("unchanged: admin_approvals.html cache buster not changed")
else:
    print("note: admin_approvals.html missing, skipped cache buster")

print("done")
