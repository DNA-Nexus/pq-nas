#!/usr/bin/env python3
from pathlib import Path
import json
import re
import sys

routes = Path("server/src/routes_v5.cc")
js = Path("server/src/static/admin_approvals.js")
html = Path("server/src/static/admin_approvals.html")
i18n_dir = Path("server/src/static/i18n")

for p in (routes, js, html, i18n_dir):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

# ----------------------------------------------------------------------
# Backend: admin endpoint to disable an OPAQUE credential immediately.
# ----------------------------------------------------------------------
s = routes.read_text()

if "/api/admin/auth/opaque/credential/disable" not in s:
    anchor = "    // ---- GET /api/admin/auth/opaque/onboarding/status ----"
    if anchor not in s:
        print("ERROR: onboarding status route anchor not found", file=sys.stderr)
        sys.exit(1)

    route = r'''
    // ---- POST /api/admin/auth/opaque/credential/disable ----
    //
    // Admin-only immediate credential disable.
    //
    // Use this for "force reset": the old OPAQUE password stops working
    // immediately, before the user opens the reset link.
    srv.Post("/api/admin/auth/opaque/credential/disable", [&](const httplib::Request& req, httplib::Response& res) {
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
            routes_v5_audit_password(ctx, req, "opaque.credential_disable", "deny", "", actor_fp, "not_admin");
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
            routes_v5_audit_password(ctx, req, "opaque.credential_disable", "deny", "", actor_fp, "forbidden_password_fallback_field");
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
            routes_v5_audit_password(ctx, req, "opaque.credential_disable", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.credential_disable", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
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
            routes_v5_audit_password(ctx, req, "opaque.credential_disable", "deny", login, actor_fp, "opaque_credential_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_credential_missing"}}.dump());
            return;
        }

        pqnas::OpaqueCredentialRec rec = *existing;
        rec.enabled = false;
        rec.updated_at = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.credential_disable", "deny", login, actor_fp, "opaque_credentials_save_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.credential_disable", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"credential_enabled", false},
            {"user_status", user->status},
            {"ready_for_login", false}
        }.dump());
    });

'''
    s = s.replace(anchor, route + "\n" + anchor, 1)
    print("patched routes_v5.cc: added OPAQUE credential disable endpoint")
else:
    print("unchanged: credential disable endpoint already exists")

old_state = '''            if (u.status == "revoked") {
                onboarding_state = "revoked";
            } else if (credential_exists && credential_enabled && u.status == "enabled") {
                onboarding_state = "setup_done";
            } else if (token_state == "active") {
                onboarding_state = "setup_link_created";
            } else if (token_state == "expired") {
                onboarding_state = "setup_link_expired";
            } else {
                onboarding_state = "waiting_password";
            }
'''
new_state = '''            if (u.status == "revoked") {
                onboarding_state = "revoked";
            } else if (credential_exists && credential_enabled && u.status == "enabled") {
                onboarding_state = "setup_done";
            } else if (token_state == "active") {
                onboarding_state = "setup_link_created";
            } else if (token_state == "expired") {
                onboarding_state = "setup_link_expired";
            } else if (credential_exists && !credential_enabled) {
                onboarding_state = "reset_required";
            } else {
                onboarding_state = "waiting_password";
            }
'''
if old_state in s:
    s = s.replace(old_state, new_state, 1)
    print("patched routes_v5.cc: added reset_required onboarding state")
elif "reset_required" in s:
    print("unchanged: reset_required already present")
else:
    print("ERROR: onboarding state anchor not found", file=sys.stderr)
    sys.exit(1)

routes.write_text(s)

# ----------------------------------------------------------------------
# Frontend: add force-reset action and i18n keys in JS.
# ----------------------------------------------------------------------
j = js.read_text()

replacements = {
    'if (s === "waiting_password") return "Odottaa salasanan asetusta";':
        'if (s === "waiting_password") return tr("admin.approvals.opaque.state.waiting_password", null, "Odottaa salasanan asetusta");',
    'if (s === "setup_link_created") return "Setup-linkki luotu";':
        'if (s === "setup_link_created") return tr("admin.approvals.opaque.state.setup_link_created", null, "Setup-linkki luotu");',
    'if (s === "setup_link_expired") return "Setup-linkki vanhentunut";':
        'if (s === "setup_link_expired") return tr("admin.approvals.opaque.state.setup_link_expired", null, "Setup-linkki vanhentunut");',
    'if (s === "setup_done") return "Setup valmis";':
        'if (s === "setup_done") return tr("admin.approvals.opaque.state.setup_done", null, "Setup valmis");',
    'if (s === "revoked") return "Revoked / peruttu";':
        'if (s === "revoked") return tr("admin.approvals.opaque.state.revoked", null, "Revoked / peruttu");',
    'return s || "Tuntematon";':
        'if (s === "reset_required") return tr("admin.approvals.opaque.state.reset_required", null, "Reset vaaditaan");\n    return s || tr("admin.approvals.opaque.state.unknown", null, "Tuntematon");',
    '`<tr><td colspan="7" class="muted">Ei OPAQUE onboarding -rivejä.</td></tr>`':
        '`<tr><td colspan="7" class="muted">${esc(tr("admin.approvals.opaque.no_rows", null, "Ei OPAQUE onboarding -rivejä."))}</td></tr>`',
    '`Aktiivinen linkki, vanhenee: ${epochLabel(row.token_expires_at)}`':
        '`' + '${tr("admin.approvals.opaque.active_link_expires", null, "Aktiivinen linkki, vanhenee:")} ${epochLabel(row.token_expires_at)}`',
    '`Linkki käytetty: ${epochLabel(row.token_used_at)}`':
        '`' + '${tr("admin.approvals.opaque.link_used", null, "Linkki käytetty:")} ${epochLabel(row.token_used_at)}`',
    '`Linkki vanhentui: ${epochLabel(row.token_expires_at)}`':
        '`' + '${tr("admin.approvals.opaque.link_expired", null, "Linkki vanhentui:")} ${epochLabel(row.token_expires_at)}`',
    '"Ei aktiivista linkkiä"':
        'tr("admin.approvals.opaque.no_active_link", null, "Ei aktiivista linkkiä")',
    '`Credential: ${row.credential_enabled ? "enabled" : "disabled"} ${row.credential_updated_at || ""}`':
        '`${tr("admin.approvals.opaque.credential", null, "Credential")}: ${row.credential_enabled ? tr("admin.approvals.opaque.credential_enabled", null, "enabled") : tr("admin.approvals.opaque.credential_disabled", null, "disabled")} ${row.credential_updated_at || ""}`',
    '"Credential puuttuu"':
        'tr("admin.approvals.opaque.credential_missing", null, "Credential puuttuu")',
    'const linkText = row.credential_exists ? "Luo reset-linkki" : "Luo setup-linkki";':
        'const linkText = row.credential_exists ? tr("admin.approvals.opaque.create_reset_link", null, "Luo reset-linkki") : tr("admin.approvals.opaque.create_setup_link", null, "Luo setup-linkki");',
    '"Peru / revoke"':
        'tr("admin.approvals.opaque.revoke_cancel", null, "Peru / revoke")',
}

for old, new in replacements.items():
    if old in j:
        j = j.replace(old, new)

if 'if (s === "reset_required") return tr("admin.approvals.opaque.state.reset_required"' not in j:
    print("ERROR: failed to patch onboardingStateLabel reset_required", file=sys.stderr)
    sys.exit(1)

if "async function forceOpaqueReset" not in j:
    anchor = '''async function createOpaqueSetupLink(row) {
    const j = await apiPost("/api/admin/auth/opaque/enrollment-token/create", {
        login: row.login,
        fingerprint: row.fingerprint,
        purpose: row.credential_exists ? "reset_password" : "new_user",
        enable_user_on_finish: true,
        expires_in_seconds: 86400
    });

    await showSetupLinkModal(row, j);
    await refresh();
}
'''
    if anchor not in j:
        print("ERROR: createOpaqueSetupLink anchor not found", file=sys.stderr)
        sys.exit(1)

    force_fn = r'''

async function forceOpaqueReset(row) {
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
    j = j.replace(anchor, anchor + force_fn, 1)
    print("patched admin_approvals.js: added forceOpaqueReset()")
else:
    print("unchanged: forceOpaqueReset already present")

old_button = '''                ${canCreateLink ? `<button class="pq-btn secondary" data-act="opaque-setup" data-fp="${esc(row.fingerprint)}" type="button">${esc(linkText)}</button>` : ""}
                <button class="pq-btn secondary" data-act="opaque-revoke" data-fp="${esc(row.fingerprint)}" type="button">Peru / revoke</button>
'''
new_button = '''                ${canCreateLink ? `<button class="pq-btn secondary" data-act="opaque-setup" data-fp="${esc(row.fingerprint)}" type="button">${esc(linkText)}</button>` : ""}
                ${row.credential_exists && row.credential_enabled && row.onboarding_state !== "revoked" ? `<button class="pq-btn danger" data-act="opaque-force-reset" data-fp="${esc(row.fingerprint)}" type="button">${esc(tr("admin.approvals.opaque.force_reset", null, "Pakota reset"))}</button>` : ""}
                <button class="pq-btn secondary" data-act="opaque-revoke" data-fp="${esc(row.fingerprint)}" type="button">${esc(tr("admin.approvals.opaque.revoke_cancel", null, "Peru / revoke"))}</button>
'''
if old_button in j:
    j = j.replace(old_button, new_button, 1)
    print("patched admin_approvals.js: added force reset button")
elif 'data-act="opaque-force-reset"' in j:
    print("unchanged: force reset button already present")
else:
    print("ERROR: button insertion anchor not found", file=sys.stderr)
    sys.exit(1)

old_handler_anchor = '''            if (act === "opaque-revoke") {
'''
new_handler = '''            if (act === "opaque-force-reset") {
                try {
                    await forceOpaqueReset(row);
                } catch (e) {
                    setMsg(tr("admin.approvals.opaque.error", { error: e.message }, "Virhe: " + e.message));
                }
                return;
            }

            if (act === "opaque-revoke") {
'''
if old_handler_anchor in j and 'act === "opaque-force-reset"' not in j:
    j = j.replace(old_handler_anchor, new_handler, 1)
    print("patched admin_approvals.js: added force reset handler")
elif 'act === "opaque-force-reset"' in j:
    print("unchanged: force reset handler already present")
else:
    print("ERROR: handler anchor not found", file=sys.stderr)
    sys.exit(1)

# More user-visible strings in modal/status; patch if exact text exists.
more = {
    'title: "Setup-linkki luotu",':
        'title: tr("admin.approvals.opaque.setup_link_created_title", null, "Setup-linkki luotu"),',
    '{ label: "Käyttäjä", value: row.name || row.login || "" },':
        '{ label: tr("admin.approvals.opaque.user", null, "Käyttäjä"), value: row.name || row.login || "" },',
    '{ label: "Vanhenee", value: epochLabel(tokenResponse.expires_at) || String(tokenResponse.expires_at || "") },':
        '{ label: tr("admin.approvals.opaque.expires", null, "Vanhenee"), value: epochLabel(tokenResponse.expires_at) || String(tokenResponse.expires_at || "") },',
    '{ label: "Setup URL", value: setupUrl, mono: true },':
        '{ label: tr("admin.approvals.opaque.setup_url", null, "Setup URL"), value: setupUrl, mono: true },',
    'note: "Kopioi tämä linkki käyttäjälle. Linkki näytetään tässä muodossa vain kerran.",':
        'note: tr("admin.approvals.opaque.copy_note", null, "Kopioi tämä linkki käyttäjälle. Linkki näytetään tässä muodossa vain kerran."),',
    'confirmText: "Kopioi linkki",':
        'confirmText: tr("admin.approvals.opaque.copy_link", null, "Kopioi linkki"),',
    'cancelText: "Sulje",':
        'cancelText: tr("admin.approvals.opaque.close", null, "Sulje"),',
    'setMsg("Setup-linkki kopioitu leikepöydälle");':
        'setMsg(tr("admin.approvals.opaque.copied", null, "Setup-linkki kopioitu leikepöydälle"));',
    'setMsg("Luodaan OPAQUE setup-linkkiä…");':
        'setMsg(tr("admin.approvals.opaque.creating_setup", null, "Luodaan OPAQUE setup-linkkiä…"));',
    'setMsg("Virhe: " + e.message);':
        'setMsg(tr("admin.approvals.opaque.error", { error: e.message }, "Virhe: " + e.message));',
    'title: "Peru OPAQUE onboarding?",':
        'title: tr("admin.approvals.opaque.revoke_title", null, "Peru OPAQUE onboarding?"),',
    'note: "Tämä asettaa käyttäjän revoked-tilaan. Olemassa oleva setup-linkki ei saa enää herättää käyttäjää takaisin.",':
        'note: tr("admin.approvals.opaque.revoke_note", null, "Tämä asettaa käyttäjän revoked-tilaan. Olemassa oleva setup-linkki ei saa enää herättää käyttäjää takaisin."),',
    'confirmText: "Peru / revoke",':
        'confirmText: tr("admin.approvals.opaque.revoke_cancel", null, "Peru / revoke"),',
    'setMsg("Perutaan…");':
        'setMsg(tr("admin.approvals.opaque.revoking", null, "Perutaan…"));',
    'setMsg("Peruttu / revoked");':
        'setMsg(tr("admin.approvals.opaque.revoked_msg", null, "Peruttu / revoked"));',
    'note: "Poistaa users.json-rivin. Käytä vain testidatan siivoukseen.",':
        'note: tr("admin.approvals.opaque.delete_note", null, "Poistaa users.json-rivin. Käytä vain testidatan siivoukseen."),',
    'setMsg("Loading auth mode…");':
        'setMsg(tr("admin.approvals.opaque.loading_auth", null, "Loading auth mode…"));',
    'setMsg("Ladataan OPAQUE onboarding…");':
        'setMsg(tr("admin.approvals.opaque.loading_onboarding", null, "Ladataan OPAQUE onboarding…"));',
    'setMsg(`Ladattu ${opaqueOnboarding.length} OPAQUE onboarding -riviä`);':
        'setMsg(tr("admin.approvals.opaque.loaded_count", { count: opaqueOnboarding.length }, `Ladattu ${opaqueOnboarding.length} OPAQUE onboarding -riviä`));',
}
for old, new in more.items():
    if old in j:
        j = j.replace(old, new)

js.write_text(j)
print("patched admin_approvals.js: i18n + force reset UI")

# Cache buster.
h = html.read_text()
h = re.sub(
    r'/static/admin_approvals\.js\?v=[^"]+',
    '/static/admin_approvals.js?v=20260613-opaque-force-reset-i18n-1',
    h
)
html.write_text(h)
print("patched admin_approvals.html cache buster")

# ----------------------------------------------------------------------
# i18n: add keys to all existing language JSON files.
# ----------------------------------------------------------------------
translations = {
    "en": {
        "admin.approvals.opaque.state.waiting_password": "Waiting for password setup",
        "admin.approvals.opaque.state.setup_link_created": "Setup link created",
        "admin.approvals.opaque.state.setup_link_expired": "Setup link expired",
        "admin.approvals.opaque.state.setup_done": "Setup complete",
        "admin.approvals.opaque.state.revoked": "Revoked / cancelled",
        "admin.approvals.opaque.state.reset_required": "Reset required",
        "admin.approvals.opaque.state.unknown": "Unknown",
        "admin.approvals.opaque.no_rows": "No OPAQUE onboarding rows.",
        "admin.approvals.opaque.active_link_expires": "Active link expires:",
        "admin.approvals.opaque.link_used": "Link used:",
        "admin.approvals.opaque.link_expired": "Link expired:",
        "admin.approvals.opaque.no_active_link": "No active link",
        "admin.approvals.opaque.credential": "Credential",
        "admin.approvals.opaque.credential_enabled": "enabled",
        "admin.approvals.opaque.credential_disabled": "disabled",
        "admin.approvals.opaque.credential_missing": "Credential missing",
        "admin.approvals.opaque.create_setup_link": "Create setup link",
        "admin.approvals.opaque.create_reset_link": "Create reset link",
        "admin.approvals.opaque.force_reset": "Force reset",
        "admin.approvals.opaque.force_reset_title": "Force password reset",
        "admin.approvals.opaque.force_reset_note": "The old OPAQUE password stops working immediately. The user can sign in only after completing the new reset link.",
        "admin.approvals.opaque.force_reset_confirm": "Force reset",
        "admin.approvals.opaque.force_resetting": "Forcing reset…",
        "admin.approvals.opaque.revoke_cancel": "Cancel / revoke",
        "admin.approvals.opaque.revoke_title": "Cancel OPAQUE onboarding?",
        "admin.approvals.opaque.revoke_note": "This sets the user to revoked. An existing setup link must not re-enable the user.",
        "admin.approvals.opaque.revoking": "Revoking…",
        "admin.approvals.opaque.revoked_msg": "Cancelled / revoked",
        "admin.approvals.opaque.setup_link_created_title": "Setup link created",
        "admin.approvals.opaque.copy_link": "Copy link",
        "admin.approvals.opaque.close": "Close",
        "admin.approvals.opaque.user": "User",
        "admin.approvals.opaque.expires": "Expires",
        "admin.approvals.opaque.setup_url": "Setup URL",
        "admin.approvals.opaque.copy_note": "Copy this link to the user. The link is shown in this form only once.",
        "admin.approvals.opaque.copied": "Setup link copied to clipboard",
        "admin.approvals.opaque.creating_setup": "Creating OPAQUE setup link…",
        "admin.approvals.opaque.error": "Error: {error}",
        "admin.approvals.opaque.delete_note": "Removes the users.json row. Use only for cleaning test data.",
        "admin.approvals.opaque.loading_auth": "Loading auth mode…",
        "admin.approvals.opaque.loading_onboarding": "Loading OPAQUE onboarding…",
        "admin.approvals.opaque.loaded_count": "Loaded {count} OPAQUE onboarding rows"
    },
    "fi": {
        "admin.approvals.opaque.state.waiting_password": "Odottaa salasanan asetusta",
        "admin.approvals.opaque.state.setup_link_created": "Setup-linkki luotu",
        "admin.approvals.opaque.state.setup_link_expired": "Setup-linkki vanhentunut",
        "admin.approvals.opaque.state.setup_done": "Setup valmis",
        "admin.approvals.opaque.state.revoked": "Revoked / peruttu",
        "admin.approvals.opaque.state.reset_required": "Reset vaaditaan",
        "admin.approvals.opaque.state.unknown": "Tuntematon",
        "admin.approvals.opaque.no_rows": "Ei OPAQUE onboarding -rivejä.",
        "admin.approvals.opaque.active_link_expires": "Aktiivinen linkki, vanhenee:",
        "admin.approvals.opaque.link_used": "Linkki käytetty:",
        "admin.approvals.opaque.link_expired": "Linkki vanhentui:",
        "admin.approvals.opaque.no_active_link": "Ei aktiivista linkkiä",
        "admin.approvals.opaque.credential": "Credential",
        "admin.approvals.opaque.credential_enabled": "enabled",
        "admin.approvals.opaque.credential_disabled": "disabled",
        "admin.approvals.opaque.credential_missing": "Credential puuttuu",
        "admin.approvals.opaque.create_setup_link": "Luo setup-linkki",
        "admin.approvals.opaque.create_reset_link": "Luo reset-linkki",
        "admin.approvals.opaque.force_reset": "Pakota reset",
        "admin.approvals.opaque.force_reset_title": "Pakota salasanan reset",
        "admin.approvals.opaque.force_reset_note": "Vanha OPAQUE-salasana lakkaa toimimasta heti. Käyttäjä pääsee sisään vasta uuden reset-linkin suorittamisen jälkeen.",
        "admin.approvals.opaque.force_reset_confirm": "Pakota reset",
        "admin.approvals.opaque.force_resetting": "Pakotetaan reset…",
        "admin.approvals.opaque.revoke_cancel": "Peru / revoke",
        "admin.approvals.opaque.revoke_title": "Peru OPAQUE onboarding?",
        "admin.approvals.opaque.revoke_note": "Tämä asettaa käyttäjän revoked-tilaan. Olemassa oleva setup-linkki ei saa enää herättää käyttäjää takaisin.",
        "admin.approvals.opaque.revoking": "Perutaan…",
        "admin.approvals.opaque.revoked_msg": "Peruttu / revoked",
        "admin.approvals.opaque.setup_link_created_title": "Setup-linkki luotu",
        "admin.approvals.opaque.copy_link": "Kopioi linkki",
        "admin.approvals.opaque.close": "Sulje",
        "admin.approvals.opaque.user": "Käyttäjä",
        "admin.approvals.opaque.expires": "Vanhenee",
        "admin.approvals.opaque.setup_url": "Setup URL",
        "admin.approvals.opaque.copy_note": "Kopioi tämä linkki käyttäjälle. Linkki näytetään tässä muodossa vain kerran.",
        "admin.approvals.opaque.copied": "Setup-linkki kopioitu leikepöydälle",
        "admin.approvals.opaque.creating_setup": "Luodaan OPAQUE setup-linkkiä…",
        "admin.approvals.opaque.error": "Virhe: {error}",
        "admin.approvals.opaque.delete_note": "Poistaa users.json-rivin. Käytä vain testidatan siivoukseen.",
        "admin.approvals.opaque.loading_auth": "Ladataan auth-tila…",
        "admin.approvals.opaque.loading_onboarding": "Ladataan OPAQUE onboarding…",
        "admin.approvals.opaque.loaded_count": "Ladattu {count} OPAQUE onboarding -riviä"
    }
}

# Good-enough translations for other UI languages.
derived = {
    "sv": ("Väntar på lösenordsinställning", "Skapa återställningslänk", "Tvinga återställning"),
    "de": ("Wartet auf Passworteinrichtung", "Reset-Link erstellen", "Reset erzwingen"),
    "es": ("Esperando configuración de contraseña", "Crear enlace de restablecimiento", "Forzar restablecimiento"),
    "fr": ("En attente de configuration du mot de passe", "Créer un lien de réinitialisation", "Forcer la réinitialisation"),
    "it": ("In attesa della configurazione della password", "Crea link di reimpostazione", "Forza reimpostazione"),
    "et": ("Ootab parooli seadistamist", "Loo lähtestuslink", "Sundi lähtestus"),
    "pl": ("Oczekuje na ustawienie hasła", "Utwórz link resetowania", "Wymuś reset"),
    "tr": ("Parola kurulumu bekleniyor", "Sıfırlama bağlantısı oluştur", "Sıfırlamayı zorla"),
    "uk": ("Очікує налаштування пароля", "Створити посилання скидання", "Примусове скидання"),
    "zh": ("等待设置密码", "创建重置链接", "强制重置"),
}

for lang, (waiting, reset_link, force_reset) in derived.items():
    base = dict(translations["en"])
    base["admin.approvals.opaque.state.waiting_password"] = waiting
    base["admin.approvals.opaque.create_reset_link"] = reset_link
    base["admin.approvals.opaque.force_reset"] = force_reset
    base["admin.approvals.opaque.force_reset_title"] = force_reset
    translations[lang] = base

for p in sorted(i18n_dir.glob("*.json")):
    lang = p.stem
    data = json.loads(p.read_text())
    add = translations.get(lang, translations["en"])
    changed = False
    for k, v in add.items():
        if data.get(k) != v:
            data[k] = v
            changed = True
    if changed:
        p.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n")
        print(f"patched i18n: {p}")

print("done")
