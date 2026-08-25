#!/usr/bin/env python3

from pathlib import Path

wiring = Path(
    "server/src/routes/routes_admin_stats_badges.inc"
).read_text()

workspace_route = Path(
    "server/src/routes/routes_workspaces_files.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


verify_anchor = wiring.find(
    "ws_file_deps.session_cookie_verify ="
)

verify_end = wiring.find(
    "ws_file_deps.b64_std_decode =",
    verify_anchor,
)

verify_block = (
    wiring[verify_anchor:verify_end]
    if verify_anchor >= 0 and verify_end > verify_anchor
    else ""
)

mint_anchor = wiring.find(
    "ws_external_session_deps.session_cookie_mint ="
)

mint_end = wiring.find(
    "ws_external_session_deps.b64_std =",
    mint_anchor,
)

mint_block = (
    wiring[mint_anchor:mint_end]
    if mint_anchor >= 0 and mint_end > mint_anchor
    else ""
)

cookie_actor_anchor = workspace_route.find(
    "bool try_workspace_external_cookie_actor_local("
)

cookie_actor_end = workspace_route.find(
    "bool require_workspace_file_actor_for_workspace_local(",
    cookie_actor_anchor,
)

cookie_actor_block = (
    workspace_route[cookie_actor_anchor:cookie_actor_end]
    if cookie_actor_anchor >= 0 and
    cookie_actor_end > cookie_actor_anchor
    else ""
)

require(
    verify_anchor >= 0 and
    verify_end > verify_anchor,
    "workspace session verify wiring block missing",
)

require(
    "workspace_session_cookie_verify(" in verify_block,
    "workspace file auth must use domain-separated workspace verifier",
)

require(
    "return session_cookie_verify(" not in verify_block,
    "workspace file auth must not verify with raw COOKIE_KEY",
)

require(
    mint_anchor >= 0 and
    mint_end > mint_anchor,
    "workspace session mint wiring block missing",
)

require(
    "workspace_session_cookie_mint(" in mint_block,
    "workspace session mint must use domain-separated workspace minter",
)

require(
    "return session_cookie_mint(" not in mint_block,
    "workspace session mint must not sign with raw COOKIE_KEY",
)

# Security: NEW-003 was caused by first-match credential parsing. The
# workspace session must use the shared strict parser that rejects duplicate
# Cookie headers, duplicate target names and malformed cookie syntax.
require(
    cookie_actor_anchor >= 0 and
    cookie_actor_end > cookie_actor_anchor,
    "workspace external cookie actor helper missing",
)

require(
    "extract_strict_session_cookie(" in cookie_actor_block,
    "workspace session must use strict cookie extraction",
)

require(
    '"pqnas_workspace_session"' in cookie_actor_block,
    "workspace strict parser must target pqnas_workspace_session",
)

require(
    "cookie_value_local(" not in workspace_route,
    "legacy first-match workspace cookie parser returned",
)

require(
    'ws_header_value(req, "Cookie")' not in workspace_route,
    "workspace credential parsing must not select first Cookie header",
)

if errors:
    for error in errors:
        print("FAIL:", error)
    raise SystemExit(1)

print("OK: workspace session domain-separation wiring guards passed")
