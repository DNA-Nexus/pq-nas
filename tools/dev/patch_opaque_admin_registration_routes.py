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

replace_once(
    "server/src/routes_v5.cc",
    '#include "opaque_credentials.h"\n',
    '#include "opaque_credentials.h"\n#include "opaque_helper_client.h"\n',
)

replace_once(
    "server/src/routes_v5.cc",
    "\nvoid register_routes_v5(httplib::Server& srv, const RoutesV5Context& ctx) {\n",
    r'''
static bool routes_v5_opaque_backend_ready_for_registration(
    const pqnas::OpaqueBackendStatus& status) {
    return status.credentials_file_exists &&
           status.credentials_file_readable &&
           status.credentials_store_valid &&
           status.server_setup_file_exists &&
           status.server_setup_file_readable &&
           status.server_setup_valid &&
           status.helper_exists &&
           status.helper_executable &&
           status.helper_version_ok &&
           status.helper_self_test_ok;
}

static bool routes_v5_has_forbidden_password_fallback_field(const nlohmann::json& j) {
    return j.contains("password") ||
           j.contains("plaintext_password") ||
           j.contains("password_hash") ||
           j.contains("classic_password_hash") ||
           j.contains("argon2id_hash");
}

static bool routes_v5_helper_result_json(
    const pqnas::OpaqueHelperClientResult& result,
    nlohmann::json& out,
    std::string& err) {
    if (!result.ok) {
        err = result.error.empty() ? "opaque_helper_failed" : result.error;
        return false;
    }

    try {
        out = nlohmann::json::parse(result.output);
    } catch (const std::exception& e) {
        err = std::string("opaque_helper_json_parse_failed: ") + e.what();
        return false;
    }

    if (!out.is_object()) {
        err = "opaque_helper_json_not_object";
        return false;
    }

    if (!out.value("ok", false)) {
        err = out.value("error", "opaque_helper_reported_failure");
        return false;
    }

    return true;
}


void register_routes_v5(httplib::Server& srv, const RoutesV5Context& ctx) {
''',
)

routes_block = r'''
    // ---- POST /api/admin/auth/opaque/registration/start ----
    //
    // Admin-only OPAQUE registration start.
    //
    // This runs the server side of OPAQUE registration start through the helper.
    // It does not write credentials and does not enable login.
    srv.Post("/api/admin/auth/opaque/registration/start", [&](const httplib::Request& req, httplib::Response& res) {
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
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", "", actor_fp, "not_admin");
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
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));
        const std::string registration_request_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "registration_request_b64"));

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (fingerprint.empty() || fingerprint.size() > 160 || routes_v5_has_control_chars(fingerprint)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_fingerprint");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_fingerprint"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(registration_request_b64, 8192)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_registration_request");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_registration_request_b64"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "opaque_backend_not_ready");
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result =
            helper.register_start(status.server_setup_path, login, registration_request_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, helper_err);
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_register_start_failed"},
                {"message", helper_err}
            }.dump());
            return;
        }

        const std::string registration_response_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "registration_response_b64"));

        if (!routes_v5_is_safe_b64ish(registration_response_b64, 8192)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_helper_registration_response");
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_invalid_registration_response"}
            }.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.registration_start", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"registration_response_b64", registration_response_b64},
            {"ready_for_login", false},
            {"warning", "OPAQUE registration start completed, but OPAQUE login/session minting is still intentionally disabled."}
        }.dump());
    });

    // ---- POST /api/admin/auth/opaque/registration/finish ----
    //
    // Admin-only OPAQUE registration finish.
    //
    // This runs OPAQUE registration finish through the helper and stores the
    // resulting serialized server-side password file in opaque_credentials.json.
    // It still does not enable OPAQUE login/session minting.
    srv.Post("/api/admin/auth/opaque/registration/finish", [&](const httplib::Request& req, httplib::Response& res) {
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
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", "", actor_fp, "not_admin");
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
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));
        const std::string registration_upload_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "registration_upload_b64"));
        std::string opaque_suite =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_suite"));

        if (opaque_suite.empty()) {
            opaque_suite = "opaque-ke-4.1.0-pre.0:ristretto255:triple-dh:sha512:argon2";
        }

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (fingerprint.empty() || fingerprint.size() > 160 || routes_v5_has_control_chars(fingerprint)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_fingerprint");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_fingerprint"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(registration_upload_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_registration_upload");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_registration_upload_b64"}}.dump());
            return;
        }

        if (opaque_suite.size() > 128 || routes_v5_has_control_chars(opaque_suite)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_suite");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_suite"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "opaque_backend_not_ready");
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result = helper.register_finish(registration_upload_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, helper_err);
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_register_finish_failed"},
                {"message", helper_err}
            }.dump());
            return;
        }

        const std::string opaque_password_file_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "opaque_password_file_b64"));

        if (!routes_v5_is_safe_b64ish(opaque_password_file_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_helper_password_file");
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_invalid_password_file"}
            }.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        pqnas::OpaqueCredentialRec rec;
        rec.login = login;
        rec.fingerprint = fingerprint;
        rec.opaque_password_file_b64 = opaque_password_file_b64;
        rec.opaque_suite = opaque_suite;
        rec.enabled = j.value("enabled", true);
        rec.temporary = j.value("temporary", false);

        const auto existing = creds.get(login);
        rec.created_at = existing.has_value() ? existing->created_at : now_iso;
        rec.updated_at = now_iso;

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
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
    });

'''

replace_once(
    "server/src/routes_v5.cc",
    "    // ---- POST /api/admin/auth/opaque/enrollment/upsert ----\n",
    routes_block + "    // ---- POST /api/admin/auth/opaque/enrollment/upsert ----\n",
)

doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## Admin enrollment storage scaffold\n"
section = """## Admin registration endpoints

The server exposes admin-only OPAQUE registration endpoints:

- `POST /api/admin/auth/opaque/registration/start`
- `POST /api/admin/auth/opaque/registration/finish`

The start endpoint accepts an existing user's `login`, `fingerprint`, and a
client-produced `registration_request_b64`. It calls `OpaqueHelperClient`
against the configured helper and returns `registration_response_b64`.

The finish endpoint accepts the same `login` and `fingerprint` plus
`registration_upload_b64`. It calls the helper, receives
`opaque_password_file_b64`, and stores the result in
`opaque_credentials.json`.

Security boundary:

- both endpoints are admin-only
- login and fingerprint must match an existing user
- plaintext password and classic password-hash fallback fields are rejected
- helper output is parsed and validated before storage
- `ready_for_login` remains `false`
- public OPAQUE login endpoints still fail closed
- no `pqnas_session` is minted by registration

"""
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    write(doc, text.replace(anchor, section + anchor, 1))
else:
    print(f"unchanged: {doc}")

print("done")
