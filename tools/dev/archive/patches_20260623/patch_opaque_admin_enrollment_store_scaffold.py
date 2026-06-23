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
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

# routes_v5.cc: include OpaqueCredentials storage.
replace_once(
    "server/src/routes_v5.cc",
    '#include "opaque_backend_status.h"\n',
    '#include "opaque_backend_status.h"\n#include "opaque_credentials.h"\n',
)

# Add helpers near other local helpers.
replace_once(
    "server/src/routes_v5.cc",
    """static std::string routes_v5_request_ip(
    const RoutesV5Context& ctx,
    const httplib::Request& req
) {
""",
    """static std::string routes_v5_trim_ascii_copy(const std::string& s);

static std::string routes_v5_opaque_credentials_path() {
    const char* env = std::getenv("PQNAS_OPAQUE_CREDENTIALS_PATH");
    const std::string env_path = routes_v5_trim_ascii_copy(env ? env : "");
    if (!env_path.empty()) {
        return env_path;
    }

    const char* cfg_env = std::getenv("PQNAS_CONFIG");
    const std::string cfg_path = routes_v5_trim_ascii_copy(cfg_env ? cfg_env : "");
    if (!cfg_path.empty()) {
        return (std::filesystem::path(cfg_path) / "opaque_credentials.json").string();
    }

    const char* cfg_root_env = std::getenv("PQNAS_CONFIG_ROOT");
    const std::string cfg_root_path = routes_v5_trim_ascii_copy(cfg_root_env ? cfg_root_env : "");
    if (!cfg_root_path.empty()) {
        return (std::filesystem::path(cfg_root_path) / "opaque_credentials.json").string();
    }

    return "/etc/pqnas/opaque_credentials.json";
}

static bool routes_v5_is_safe_b64ish(const std::string& s, std::size_t max_len) {
    if (s.empty() || s.size() > max_len) return false;

    for (unsigned char c : s) {
        const bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '+' ||
            c == '/' ||
            c == '=' ||
            c == '-' ||
            c == '_' ||
            c == '.';

        if (!ok) return false;
    }

    return true;
}

static std::string routes_v5_request_ip(
    const RoutesV5Context& ctx,
    const httplib::Request& req
) {
""",
)

# Add admin-only upsert endpoint before public OPAQUE login/start.
replace_once(
    "server/src/routes_v5.cc",
    """    // ---- POST /api/auth/opaque/login/start ----
""",
    """    // ---- POST /api/admin/auth/opaque/enrollment/upsert ----
    //
    // Admin-only storage scaffold for future OPAQUE enrollment.
    //
    // This endpoint does not run the OPAQUE registration protocol and does not
    // enable OPAQUE login. It only stores a serialized OPAQUE password file
    // produced by a future reviewed OPAQUE registration path.
    srv.Post("/api/admin/auth/opaque/enrollment/upsert", [&](const httplib::Request& req, httplib::Response& res) {
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
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));
        const std::string opaque_password_file_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_password_file_b64"));
        std::string opaque_suite =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_suite"));

        if (opaque_suite.empty()) {
            opaque_suite = "opaque-ke-4.1.0-pre.0:ristretto255:triple-dh:sha512:argon2";
        }

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (fingerprint.empty() || fingerprint.size() > 160 || routes_v5_has_control_chars(fingerprint)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_fingerprint");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_fingerprint"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(opaque_password_file_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_opaque_password_file");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_password_file_b64"}}.dump());
            return;
        }

        if (opaque_suite.size() > 128 || routes_v5_has_control_chars(opaque_suite)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_suite");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_suite"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!status.credentials_file_exists ||
            !status.credentials_file_readable ||
            !status.credentials_store_valid ||
            !status.server_setup_file_exists ||
            !status.server_setup_file_readable ||
            !status.server_setup_valid ||
            !status.helper_exists ||
            !status.helper_executable ||
            !status.helper_version_ok ||
            !status.helper_self_test_ok) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "opaque_backend_not_ready");
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
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
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "opaque_credentials_save_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"opaque_suite", opaque_suite},
            {"enabled", rec.enabled},
            {"temporary", rec.temporary},
            {"ready_for_login", false},
            {"warning", "OPAQUE enrollment was stored, but OPAQUE login/session minting is still intentionally disabled."}
        }.dump());
    });

    // ---- POST /api/auth/opaque/login/start ----
""",
)

# Document the scaffold.
doc = "docs/technical/opaque_login_design.md"
text = read(doc)
needle = "## Backup and restore notes\n"
insert = """## Admin enrollment storage scaffold

`POST /api/admin/auth/opaque/enrollment/upsert` is an admin-only storage
scaffold for future OPAQUE enrollment.

It stores a serialized OPAQUE server-side password file for an existing
PQ-NAS user and login. It does not perform the OPAQUE registration
protocol itself, does not enable public self-registration, does not verify
login-finish transcripts, and does not mint `pqnas_session`.

Required body fields:

- `login`
- `fingerprint`
- `opaque_password_file_b64`

Optional body fields:

- `opaque_suite`
- `enabled`
- `temporary`

The endpoint requires:

- authenticated admin session
- existing user fingerprint
- login matching the user's email
- readable and valid OPAQUE credentials store
- readable and valid OPAQUE server setup
- working OPAQUE helper preflight

Even after an enrollment record is stored, `ready_for_login` remains
`false` until real OPAQUE login verification and session minting are
implemented.

"""
if "## Admin enrollment storage scaffold" not in text:
    if needle not in text:
        die("doc anchor not found")
    text = text.replace(needle, insert + needle, 1)
    write(doc, text)
else:
    print(f"unchanged: {doc}")

print("done")
