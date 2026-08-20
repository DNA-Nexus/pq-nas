#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

ROUTES = ROOT / "server/src/routes/routes_v5.cc"
VERIFY = ROOT / "server/src/verify_login_common.cc"
ROUTES_H = ROOT / "server/include/routes_v5.h"
VERIFY_H = ROOT / "server/include/verify_login_common.h"
MAIN = ROOT / "server/src/main.cpp"

failures = 0


def expect(condition: bool, message: str) -> None:
    global failures

    if not condition:
        failures += 1
        print(f"FAIL: {message}", file=sys.stderr)


routes = ROUTES.read_text()
verify = VERIFY.read_text()
routes_h = ROUTES_H.read_text()
verify_h = VERIFY_H.read_text()
main = MAIN.read_text()

guard = "pqnas::require_interactive_user_login("

# ------------------------------------------------------------------
# Direct routes:
# Security: every local user-auth / credential-minting or credential-
# lifecycle endpoint that is unsafe on Storage Nodes must hit the shared
# fail-closed policy immediately at handler entry.
# ------------------------------------------------------------------
protected_endpoints = [
    ("Post", "/api/auth/opaque/enrollment/start"),
    ("Post", "/api/auth/opaque/enrollment/finish"),
    ("Post", "/api/auth/opaque/login/start"),
    ("Post", "/api/auth/opaque/login/finish"),
    ("Post", "/api/auth/opaque/bootstrap-admin/start"),
    ("Post", "/api/auth/opaque/bootstrap-admin/finish"),
    ("Post", "/api/auth/password/bootstrap-admin"),
    ("Post", "/api/auth/password/setup-finish"),
    ("Post", "/api/auth/password/login"),
    ("Post", "/api/auth/password/recover"),
    ("Post", "/api/auth/password/set"),
    ("Post", "/api/auth/password/change"),
    ("Post", "/api/v5/consume"),
    ("Post", "/api/v5/consume_app"),
    ("Post", "/api/v5/token/refresh"),
    ("Post", "/api/v5/app_pair/start"),
    ("Post", "/api/v5/app_pair/consume"),
    ("Get", "/api/v5/app_pair/qr.svg"),
    ("Get", "/api/v5/app_pair/status"),
    ("Get", "/api/v5/app_devices"),
]


for method, route in protected_endpoints:
    marker = f'srv.{method}("{route}"'
    start = routes.find(marker)

    expect(
        start >= 0,
        f"protected route exists: {route}",
    )

    if start < 0:
        continue

    # Gate must stay at handler entry rather than after parsing,
    # authentication, state lookup, mutation or credential minting.
    window = routes[start:start + 700]

    expect(
        guard in window,
        f"protected route has early interactive-login gate: {route}",
    )


# Cleanup operations intentionally remain available on a Storage Node.
# Security: disabling credential minting must not prevent revocation.
ungated_cleanup_endpoints = [
    ("Post", "/api/v5/token/revoke"),
    ("Post", "/api/v5/app_devices/revoke"),
    ("Post", "/api/v5/app_pair/cancel"),
]


for method, route in ungated_cleanup_endpoints:
    marker = f'srv.{method}("{route}"'
    start = routes.find(marker)

    expect(
        start >= 0,
        f"cleanup route exists: {route}",
    )

    if start < 0:
        continue

    window = routes[start:start + 700]

    expect(
        guard not in window,
        f"cleanup route remains available when interactive login is disabled: {route}",
    )


# ------------------------------------------------------------------
# QR session start:
# It uses a shared session_handler rather than an inline route lambda.
# Gate must precede rate limiting, RNG, ST creation and pending state.
# ------------------------------------------------------------------
session_start = routes.find(
    "auto session_handler = "
)

expect(
    session_start >= 0,
    "/api/v5/session handler exists",
)

if session_start >= 0:
    session_guard = routes.find(
        guard,
        session_start,
    )

    session_rate_limit = routes.find(
        "routes_v5_request_ip(ctx, req)",
        session_start,
    )

    expect(
        session_guard >= 0,
        "/api/v5/session has interactive-login gate",
    )

    expect(
        session_rate_limit >= 0,
        "/api/v5/session rate-limit anchor exists",
    )

    if session_guard >= 0 and session_rate_limit >= 0:
        expect(
            session_guard < session_rate_limit,
            "/api/v5/session gate runs before rate limiting/state creation",
        )


# ------------------------------------------------------------------
# Shared QR verify:
# Security: alternate external-workspace authentication hooks must not
# bypass the Storage-Node interactive-login policy. The gate must run
# before those hooks and before first-admin/bootstrap state mutation.
# ------------------------------------------------------------------
verify_guard = verify.find(guard)
bootstrap = verify.find(
    "// Bootstrap: first verified fingerprint becomes admin if fresh install"
)

external_invite = verify.find(
    "external_invite_accept_by_st_hash"
)

external_session = verify.find(
    "external_session_accept_by_st_hash"
)

expect(
    verify_guard >= 0,
    "shared verify has interactive-login gate",
)

expect(
    bootstrap >= 0,
    "shared verify bootstrap anchor exists",
)

if verify_guard >= 0 and bootstrap >= 0:
    expect(
        verify_guard < bootstrap,
        "shared verify gate runs before first-admin bootstrap",
    )

if external_invite >= 0 and verify_guard >= 0:
    expect(
        verify_guard < external_invite,
        "shared verify gate runs before external invite authentication",
    )

if external_session >= 0 and verify_guard >= 0:
    expect(
        verify_guard < external_session,
        "shared verify gate runs before external workspace session authentication",
    )


# ------------------------------------------------------------------
# Fail-closed context defaults.
# A future caller that forgets role wiring must deny login.
# ------------------------------------------------------------------
default_line = (
    "bool interactive_user_login_allowed = false;"
)

expect(
    default_line in routes_h,
    "RoutesV5Context defaults interactive login to denied",
)

expect(
    default_line in verify_h,
    "VerifyLoginCommonContext defaults interactive login to denied",
)


# ------------------------------------------------------------------
# Production composition-root wiring.
# Standalone/Director explicitly allow normal auth; Node explicitly denies.
# ------------------------------------------------------------------
expect(
    "v5.interactive_user_login_allowed = !cluster_config.is_node();"
    in main,
    "RoutesV5Context production role policy is wired",
)

expect(
    "c.interactive_user_login_allowed = !cluster_config.is_node();"
    in main,
    "VerifyLoginCommonContext production role policy is wired",
)


if failures:
    print(
        f"FAIL: {failures} Node interactive-auth guard test(s) failed",
        file=sys.stderr,
    )
    raise SystemExit(1)

print("OK: Node interactive-auth structural guards passed")
