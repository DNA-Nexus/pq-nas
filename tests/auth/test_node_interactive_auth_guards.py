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
# Direct POST routes:
# Security: every local user-auth / credential / consume endpoint must
# hit the shared fail-closed policy immediately at handler entry.
# ------------------------------------------------------------------
protected_routes = [
    "/api/auth/opaque/enrollment/start",
    "/api/auth/opaque/enrollment/finish",
    "/api/auth/opaque/login/start",
    "/api/auth/opaque/login/finish",
    "/api/auth/opaque/bootstrap-admin/start",
    "/api/auth/opaque/bootstrap-admin/finish",
    "/api/auth/password/bootstrap-admin",
    "/api/auth/password/setup-finish",
    "/api/auth/password/login",
    "/api/auth/password/recover",
    "/api/auth/password/set",
    "/api/auth/password/change",
    "/api/v5/consume",
    "/api/v5/consume_app",
]

for route in protected_routes:
    marker = f'srv.Post("{route}"'
    start = routes.find(marker)

    expect(
        start >= 0,
        f"protected route exists: {route}",
    )

    if start < 0:
        continue

    # The gate must remain at the very beginning of the handler rather
    # than after parsing, crypto, mutation, or credential minting.
    window = routes[start:start + 700]

    expect(
        guard in window,
        f"protected route has early interactive-login gate: {route}",
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
# External workspace flows intentionally remain separate. Normal Node
# login must stop after those hooks but before first-admin bootstrap or
# users.json mutation.
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
        external_invite < verify_guard,
        "external invite flow remains before normal-user gate",
    )

if external_session >= 0 and verify_guard >= 0:
    expect(
        external_session < verify_guard,
        "external workspace session flow remains before normal-user gate",
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
