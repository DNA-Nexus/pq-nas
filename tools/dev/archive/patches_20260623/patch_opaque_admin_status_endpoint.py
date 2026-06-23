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

# 1) routes_v5.cc needs the internal diagnostic helper.
replace_once(
    "server/src/routes_v5.cc",
    """#include "password_credentials.h"
#include "dna_identity_generator.h"
""",
    """#include "password_credentials.h"
#include "dna_identity_generator.h"
#include "opaque_backend_status.h"
""",
)

# 2) Add admin-only OPAQUE status endpoint after /api/auth/config.
replace_once(
    "server/src/routes_v5.cc",
    """    // ---- POST /api/auth/opaque/login/start ----
""",
    """    // ---- GET /api/admin/auth/opaque/status ----
    //
    // Admin-only internal diagnostic endpoint.
    //
    // This intentionally exposes backend readiness details only to admins.
    // Public OPAQUE login endpoints must continue returning generic fail-closed
    // errors so callers cannot enumerate backend state or user existence.
    srv.Get("/api/admin/auth/opaque/status", [&](const httplib::Request& req, httplib::Response& res) {
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
            routes_v5_audit_password(ctx, req, "opaque.admin_status", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        std::string body = pqnas::opaque_backend_internal_diagnostic_json(status);

        if (!routes_v5_append_json_member_to_object(body, "\\"ok\\":true")) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "opaque_status_response_build_failed"}
            }.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.admin_status", "ok", "", actor_fp, "");
        reply_json(res, 200, body);
    });

    // ---- POST /api/auth/opaque/login/start ----
""",
)

# 3) Update design/status doc.
replace_once(
    "docs/technical/opaque_login_design.md",
    """- `OpaqueBackendStatus` has an internal/admin-only diagnostic JSON helper; public OPAQUE login errors remain generic.
""",
    """- `OpaqueBackendStatus` has an internal/admin-only diagnostic JSON helper; public OPAQUE login errors remain generic.
- `GET /api/admin/auth/opaque/status` exposes OPAQUE backend diagnostics to admins only; public OPAQUE login endpoints remain generic and fail-closed.
""",
)

print("done")
