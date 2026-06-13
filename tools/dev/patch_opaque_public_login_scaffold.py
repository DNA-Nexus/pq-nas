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

routes = "server/src/routes_v5.cc"
text = read(routes)

helper_block = r'''
struct RoutesV5OpaqueLoginPending {
    std::string login;
    std::string fingerprint;
    std::string server_login_state_b64;
    long expires_at = 0;
};

static std::mutex& routes_v5_opaque_login_pending_mu() {
    static std::mutex mu;
    return mu;
}

static std::unordered_map<std::string, RoutesV5OpaqueLoginPending>&
routes_v5_opaque_login_pending_map() {
    static std::unordered_map<std::string, RoutesV5OpaqueLoginPending> m;
    return m;
}

static long routes_v5_now_epoch_safe(const RoutesV5Context& ctx) {
    if (ctx.now_epoch) {
        return ctx.now_epoch();
    }

    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

static void routes_v5_opaque_login_pending_prune(long now) {
    std::lock_guard<std::mutex> lock(routes_v5_opaque_login_pending_mu());
    auto& m = routes_v5_opaque_login_pending_map();

    for (auto it = m.begin(); it != m.end();) {
        if (it->second.expires_at <= now) {
            it = m.erase(it);
        } else {
            ++it;
        }
    }
}

static std::string routes_v5_random_hex_id_128() {
    if (sodium_init() < 0) return {};

    unsigned char buf[16];
    randombytes_buf(buf, sizeof(buf));

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : buf) {
        oss << std::setw(2) << static_cast<unsigned int>(b);
    }
    return oss.str();
}

static bool routes_v5_is_safe_hex_id_128(const std::string& s) {
    if (s.size() != 32) return false;

    for (char ch : s) {
        const bool ok =
            (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F');

        if (!ok) return false;
    }

    return true;
}

static bool routes_v5_opaque_login_pending_put(
    const std::string& opaque_login_id,
    const RoutesV5OpaqueLoginPending& pending) {
    if (!routes_v5_is_safe_hex_id_128(opaque_login_id) ||
        pending.login.empty() ||
        pending.fingerprint.empty() ||
        pending.server_login_state_b64.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(routes_v5_opaque_login_pending_mu());
    auto& m = routes_v5_opaque_login_pending_map();

    if (m.size() > 4096) {
        return false;
    }

    m[opaque_login_id] = pending;
    return true;
}

static bool routes_v5_opaque_login_pending_pop(
    const std::string& opaque_login_id,
    long now,
    RoutesV5OpaqueLoginPending& out) {
    if (!routes_v5_is_safe_hex_id_128(opaque_login_id)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(routes_v5_opaque_login_pending_mu());
    auto& m = routes_v5_opaque_login_pending_map();

    auto it = m.find(opaque_login_id);
    if (it == m.end()) {
        return false;
    }

    out = it->second;
    m.erase(it);

    if (out.expires_at <= now) {
        return false;
    }

    return true;
}

'''

if "struct RoutesV5OpaqueLoginPending" not in text:
    anchor = "\n\nvoid register_routes_v5(httplib::Server& srv, const RoutesV5Context& ctx) {"
    if anchor not in text:
        die("register_routes_v5 anchor not found")
    text = text.replace(anchor, "\n" + helper_block + anchor, 1)
else:
    print("unchanged: routes_v5 opaque login pending helpers already present")

start_marker = "    // ---- POST /api/auth/opaque/login/start ----"
end_marker = "    // ---- POST /api/auth/password/bootstrap-admin ----"

if start_marker not in text or end_marker not in text:
    die("public OPAQUE login route markers not found")

new_public_routes = r'''    // ---- POST /api/auth/opaque/login/start ----
    //
    // Public OPAQUE login start.
    //
    // This verifies only the OPAQUE transcript start and returns the server
    // credential response plus an opaque in-memory login id. It never accepts
    // plaintext passwords and never mints pqnas_session.
    srv.Post("/api/auth/opaque/login/start", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_auth_disabled"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        if (routes_v5_has_forbidden_password_fallback_field(j)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", "", "", "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string credential_request_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "credential_request_b64"));

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(credential_request_b64, 8192)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "invalid_credential_request");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_credential_request_b64"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("opaque.login_start.") + login,
                ip_for_rate_limit,
                12,
                std::chrono::seconds(60))) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "rate_limited");
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_opaque_login_attempts"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "opaque_backend_not_ready");
            reply_json(res, 503, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const auto rec = creds.get(login);
        if (!rec.has_value() ||
            !rec->enabled ||
            rec->opaque_password_file_b64.empty() ||
            !routes_v5_is_safe_b64ish(rec->opaque_password_file_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "login_missing_or_disabled");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result = helper.login_start(
            status.server_setup_path,
            rec->opaque_password_file_b64,
            login,
            credential_request_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, helper_err);
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const std::string credential_response_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "credential_response_b64"));
        const std::string server_login_state_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "server_login_state_b64"));

        if (!routes_v5_is_safe_b64ish(credential_response_b64, 8192) ||
            !routes_v5_is_safe_b64ish(server_login_state_b64, 16384)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, "invalid_helper_login_start_response");
            reply_json(res, 502, json{{"ok", false}, {"error", "opaque_helper_invalid_login_start_response"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        routes_v5_opaque_login_pending_prune(now);

        std::string opaque_login_id;
        for (int i = 0; i < 8 && opaque_login_id.empty(); ++i) {
            const std::string candidate = routes_v5_random_hex_id_128();
            if (candidate.empty()) continue;

            RoutesV5OpaqueLoginPending pending;
            pending.login = login;
            pending.fingerprint = rec->fingerprint;
            pending.server_login_state_b64 = server_login_state_b64;
            pending.expires_at = now + 120;

            if (routes_v5_opaque_login_pending_put(candidate, pending)) {
                opaque_login_id = candidate;
            }
        }

        if (opaque_login_id.empty()) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, "opaque_login_state_store_failed");
            reply_json(res, 503, json{{"ok", false}, {"error", "opaque_login_state_store_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.login_start", "ok", login, rec->fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"opaque_login_id", opaque_login_id},
            {"credential_response_b64", credential_response_b64},
            {"expires_at", now + 120},
            {"ready_for_session", false},
            {"session_minting", false},
            {"warning", "OPAQUE transcript start completed, but session minting is still intentionally disabled."}
        }.dump());
    });

    // ---- POST /api/auth/opaque/login/finish ----
    //
    // Public OPAQUE login finish scaffold.
    //
    // This proves the OPAQUE transcript using the helper and returns
    // authenticated:true only for a valid transcript. It still never mints
    // pqnas_session and never sets Set-Cookie.
    srv.Post("/api/auth/opaque/login/finish", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_auth_disabled"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        if (routes_v5_has_forbidden_password_fallback_field(j)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string opaque_login_id =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_login_id"));
        const std::string credential_finalization_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "credential_finalization_b64"));

        if (!routes_v5_is_safe_hex_id_128(opaque_login_id)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "invalid_opaque_login_id");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_login_id"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(credential_finalization_b64, 8192)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "invalid_credential_finalization");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_credential_finalization_b64"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("opaque.login_finish.") + opaque_login_id,
                ip_for_rate_limit,
                12,
                std::chrono::seconds(60))) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "rate_limited");
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_opaque_login_attempts"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        routes_v5_opaque_login_pending_prune(now);

        RoutesV5OpaqueLoginPending pending;
        if (!routes_v5_opaque_login_pending_pop(opaque_login_id, now, pending)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "opaque_login_state_missing");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, "opaque_backend_not_ready");
            reply_json(res, 503, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result = helper.login_finish(
            pending.server_login_state_b64,
            credential_finalization_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, helper_err);
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        if (!helper_json.value("authenticated", false)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, "not_authenticated");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        std::string account_status = "unknown";
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
    });

'''

start = text.index(start_marker)
end = text.index(end_marker)
text = text[:start] + new_public_routes + text[end:]
write(routes, text)

# Update runtime test so it validates public scaffold when server is in OPAQUE mode,
# but still passes/skips that part when the runtime is in password mode.
test = "tests/opaque_admin_registration/test_opaque_admin_registration_positive_runtime.py"
text = read(test)

old = '''    status_code, headers, login_start = request_json(
        "POST",
        "/api/auth/opaque/login/start",
        {
            "login": login,
            "client_login_start_b64": "QUJD",
        },
    )

    require(
        status_code in (404, 501),
        f"public OPAQUE login start must remain disabled/fail-closed, got {status_code}: {login_start}",
    )
    require(no_session_cookie(headers), "public OPAQUE login start must not set session cookie")
'''

new = '''    client_login_start = cargo_fixture(cargo_manifest, "login-start-fixture")
    client_login_state_b64 = client_login_start.get("client_login_state_b64", "")
    credential_request_b64 = client_login_start.get("credential_request_b64", "")
    require(client_login_state_b64, f"client login start missing state: {client_login_start}")
    require(credential_request_b64, f"client login start missing request: {client_login_start}")

    status_code, headers, login_start = request_json(
        "POST",
        "/api/auth/opaque/login/start",
        {
            "login": login,
            "credential_request_b64": credential_request_b64,
        },
    )

    if status_code == 404 and login_start.get("error") == "opaque_auth_disabled":
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
'''

if old not in text:
    die("runtime test public login block not found")
text = text.replace(old, new, 1)
write(test, text)

# Docs.
doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## C++ helper client login wrappers\n"
section = '''## Public OPAQUE login scaffold

The public HTTP routes now perform the OPAQUE transcript through the helper when
`PQNAS_LOGIN_MODE=opaque`:

- `POST /api/auth/opaque/login/start`
  - input: `login`, `credential_request_b64`
  - output: `opaque_login_id`, `credential_response_b64`
  - stores serialized server login state only in a short-lived in-memory map

- `POST /api/auth/opaque/login/finish`
  - input: `opaque_login_id`, `credential_finalization_b64`
  - output: `authenticated: true` only after helper verification

Security boundary:

- no plaintext password field is accepted
- forbidden fallback password/hash fields are rejected
- server login state is not returned to the browser
- `opaque_login_id` is one-time-use and short-lived
- no `pqnas_session` cookie is minted
- `ready_for_session` remains `false`
- session minting will be reviewed in a separate commit

'''
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    text = text.replace(anchor, section + anchor, 1)
    write(doc, text)
else:
    print(f"unchanged: {doc}")

print("done")
