#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def p(rel: str) -> Path:
    return ROOT / rel

def read(rel: str) -> str:
    path = p(rel)
    if not path.exists():
        die(f"missing file: {rel}")
    return path.read_text(encoding="utf-8")

def write(rel: str, text: str) -> None:
    p(rel).write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}")
    write(rel, text.replace(old, new, 1))

# -------------------------
# server/src/routes_v5.cc
# -------------------------

routes = "server/src/routes_v5.cc"

replace_once(
    routes,
    '''        const auto rec = creds.get(login);
        if (!rec.has_value() ||
            !rec->enabled ||
            rec->opaque_password_file_b64.empty() ||
            !routes_v5_is_safe_b64ish(rec->opaque_password_file_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "login_missing_or_disabled");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
''',
    '''        const auto rec = creds.get(login);
        if (!rec.has_value() ||
            !rec->enabled ||
            rec->opaque_password_file_b64.empty() ||
            !routes_v5_is_safe_b64ish(rec->opaque_password_file_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "login_missing_or_disabled");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user_opt = ctx.users->get(rec->fingerprint);
        if (!user_opt.has_value() || user_opt->status != "enabled") {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, "user_disabled_or_missing");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
''',
)

replace_once(
    routes,
    '''            {"ready_for_session", false},
            {"session_minting", false},
            {"warning", "OPAQUE transcript start completed, but session minting is still intentionally disabled."}
''',
    '''            {"ready_for_session", false},
            {"session_minting", false},
            {"warning", "OPAQUE transcript start completed. Session minting happens only after login/finish."}
''',
)

replace_once(
    routes,
    '''    // Public OPAQUE login finish scaffold.
    //
    // This proves the OPAQUE transcript using the helper and returns
    // authenticated:true only for a valid transcript. It still never mints
    // pqnas_session and never sets Set-Cookie.
''',
    '''    // Public OPAQUE login finish.
    //
    // This proves the OPAQUE transcript using the helper and mints pqnas_session
    // only after successful transcript verification and enabled-user check.
''',
)

replace_once(
    routes,
    '''        std::string account_status = "unknown";
        bool login_allowed = false;

        if (ctx.users) {
            const auto user = ctx.users->get(pending.fingerprint);
            if (user.has_value()) {
                account_status = user->status;
                login_allowed = (user->status == "enabled");
            }
        }

        routes_v5_audit_password(ctx, req, "opaque.login_finish", "ok", pending.login, pending.fingerprint, "session_minting_disabled");

        reply_json(res, 200, json{
            {"ok", true},
            {"authenticated", true},
            {"login", pending.login},
            {"fingerprint", pending.fingerprint},
            {"account_status", account_status},
            {"login_allowed", login_allowed},
            {"ready_for_session", false},
            {"session_minting", false},
            {"warning", "OPAQUE transcript verified, but pqnas_session minting is still intentionally disabled."}
        }.dump());
''',
    '''        if (!ctx.users || !ctx.users_path || ctx.users_path->empty() ||
            !ctx.cookie_key || !ctx.session_cookie_mint) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_session_mint_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(pending.fingerprint);
        if (!user.has_value() || user->status != "enabled") {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, "user_disabled_or_missing");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const long sess_iat = now;
        const long sess_exp = sess_iat + (ctx.sess_ttl ? *ctx.sess_ttl : 3600);

        const std::string fp_b64 = routes_v5_b64std_from_string(pending.fingerprint);
        if (fp_b64.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "fingerprint_b64_failed"}}.dump());
            return;
        }

        std::string cookie_val;
        if (!ctx.session_cookie_mint(ctx.cookie_key, fp_b64, sess_iat, sess_exp, cookie_val) ||
            cookie_val.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "cookie_mint_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};
        if (!now_iso.empty()) {
            ctx.users->touch_last_seen(pending.fingerprint, now_iso);
            ctx.users->save(*ctx.users_path);
        }

        const std::string set_cookie =
            std::string("pqnas_session=") + cookie_val +
            "; Path=/" +
            "; HttpOnly" +
            "; SameSite=Strict" +
            "; Secure";

        res.set_header("Set-Cookie", set_cookie);

        routes_v5_audit_password(ctx, req, "opaque.login_finish", "ok", pending.login, pending.fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"authenticated", true},
            {"login", pending.login},
            {"fingerprint", pending.fingerprint},
            {"role", user->role},
            {"expires_at", sess_exp},
            {"ready_for_session", true},
            {"session_minting", true}
        }.dump());
''',
)

# -------------------------
# Runtime test: adapt public OPAQUE checks for session minting.
# -------------------------

test = "tests/opaque_admin_registration/test_opaque_admin_registration_positive_runtime.py"

replace_once(
    test,
    '''    if status_code == 404 and login_start.get("error") == "opaque_auth_disabled":
        require(no_session_cookie(headers), "disabled public OPAQUE login start must not set session cookie")
    else:
        require(status_code == 200, f"public OPAQUE login start must return 200, got {status_code}: {login_start}")
        require(login_start.get("ok") is True, f"public OPAQUE login start failed: {login_start}")
        require(login_start.get("login") == login, f"public OPAQUE login start login mismatch: {login_start}")
        require(login_start.get("ready_for_session") is False, "login start must not be ready for session")
        require(login_start.get("session_minting") is False, "login start must keep session minting disabled")
        require(no_session_cookie(headers), "public OPAQUE login start must not set session cookie")

        opaque_login_id = login_start.get("opaque_login_id", "")
        credential_response_b64 = login_start.get("credential_response_b64", "")
        require(opaque_login_id, f"public OPAQUE login start missing id: {login_start}")
        require(credential_response_b64, f"public OPAQUE login start missing response: {login_start}")

        client_login_finish = cargo_fixture(
            cargo_manifest,
            "login-finish-fixture",
            client_login_state_b64,
            credential_response_b64,
        )
        credential_finalization_b64 = client_login_finish.get("credential_finalization_b64", "")
        require(credential_finalization_b64, f"client login finish missing finalization: {client_login_finish}")

        status_code, headers, login_finish = request_json(
            "POST",
            "/api/auth/opaque/login/finish",
            {
                "opaque_login_id": opaque_login_id,
                "credential_finalization_b64": credential_finalization_b64,
            },
        )

        require(status_code == 200, f"public OPAQUE login finish must return 200, got {status_code}: {login_finish}")
        require(login_finish.get("ok") is True, f"public OPAQUE login finish failed: {login_finish}")
        require(login_finish.get("authenticated") is True, f"public OPAQUE login finish did not authenticate: {login_finish}")
        require(login_finish.get("login") == login, f"public OPAQUE login finish login mismatch: {login_finish}")
        require(login_finish.get("fingerprint") == fingerprint, f"public OPAQUE login finish fingerprint mismatch: {login_finish}")
        require(login_finish.get("ready_for_session") is False, "login finish must not be ready for session")
        require(login_finish.get("session_minting") is False, "login finish must keep session minting disabled")
        require(no_session_cookie(headers), "public OPAQUE login finish must not set session cookie")

        if created_user:
            require(login_finish.get("account_status") == "disabled", f"created test user should remain disabled: {login_finish}")
            require(login_finish.get("login_allowed") is False, f"disabled test user must not be login_allowed: {login_finish}")
''',
    '''    if status_code == 404 and login_start.get("error") == "opaque_auth_disabled":
        require(no_session_cookie(headers), "disabled public OPAQUE login start must not set session cookie")
    elif created_user and status_code == 401:
        require(no_session_cookie(headers), "disabled test user OPAQUE login start must not set session cookie")
    else:
        require(status_code == 200, f"public OPAQUE login start must return 200, got {status_code}: {login_start}")
        require(login_start.get("ok") is True, f"public OPAQUE login start failed: {login_start}")
        require(login_start.get("login") == login, f"public OPAQUE login start login mismatch: {login_start}")
        require(login_start.get("ready_for_session") is False, "login start must not be ready for session")
        require(login_start.get("session_minting") is False, "login start must keep session minting disabled")
        require(no_session_cookie(headers), "public OPAQUE login start must not set session cookie")

        opaque_login_id = login_start.get("opaque_login_id", "")
        credential_response_b64 = login_start.get("credential_response_b64", "")
        require(opaque_login_id, f"public OPAQUE login start missing id: {login_start}")
        require(credential_response_b64, f"public OPAQUE login start missing response: {login_start}")

        client_login_finish = cargo_fixture(
            cargo_manifest,
            "login-finish-fixture",
            client_login_state_b64,
            credential_response_b64,
        )
        credential_finalization_b64 = client_login_finish.get("credential_finalization_b64", "")
        require(credential_finalization_b64, f"client login finish missing finalization: {client_login_finish}")

        status_code, headers, login_finish = request_json(
            "POST",
            "/api/auth/opaque/login/finish",
            {
                "opaque_login_id": opaque_login_id,
                "credential_finalization_b64": credential_finalization_b64,
            },
        )

        require(status_code == 200, f"public OPAQUE login finish must return 200, got {status_code}: {login_finish}")
        require(login_finish.get("ok") is True, f"public OPAQUE login finish failed: {login_finish}")
        require(login_finish.get("authenticated") is True, f"public OPAQUE login finish did not authenticate: {login_finish}")
        require(login_finish.get("login") == login, f"public OPAQUE login finish login mismatch: {login_finish}")
        require(login_finish.get("fingerprint") == fingerprint, f"public OPAQUE login finish fingerprint mismatch: {login_finish}")
        require(login_finish.get("ready_for_session") is True, "login finish must be ready for session")
        require(login_finish.get("session_minting") is True, "login finish must mint session")
        require(not no_session_cookie(headers), "public OPAQUE login finish must set pqnas_session cookie")
''',
)

# -------------------------
# Docs.
# -------------------------

doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## Public OPAQUE login scaffold\n"
section = '''## Public OPAQUE session minting

`POST /api/auth/opaque/login/finish` now mints the standard `pqnas_session`
cookie after all of these conditions are true:

1. the OPAQUE helper verifies the login transcript
2. the pending `opaque_login_id` exists and is unexpired
3. the mapped user exists in UsersRegistry
4. the user status is `enabled`
5. cookie minting callbacks are configured

The session cookie uses the same `session_cookie_mint` callback and Set-Cookie
attributes as password login:

- `HttpOnly`
- `SameSite=Strict`
- `Secure`
- `Path=/`

Disabled or missing users receive the same generic `invalid_login_or_password`
response and never get a session cookie.

'''
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    text = text.replace(anchor, section + anchor, 1)
    write(doc, text)
else:
    print(f"unchanged: {doc}")

print("done")
