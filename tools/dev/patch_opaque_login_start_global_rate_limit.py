#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("server/src/routes_v5.cc")
if not p.exists():
    print("ERROR: missing server/src/routes_v5.cc", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

old = """        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
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
"""

new = """        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
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

        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("opaque.login_start.global.") + login,
                "global",
                30,
                std::chrono::seconds(300))) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "global_rate_limited");
            res.set_header("Retry-After", "300");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_opaque_login_attempts"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
"""

if old not in s:
    print("ERROR: opaque.login_start rate-limit anchor not found", file=sys.stderr)
    sys.exit(1)

s = s.replace(old, new, 1)
p.write_text(s)

print("added global OPAQUE login/start rate limit")
