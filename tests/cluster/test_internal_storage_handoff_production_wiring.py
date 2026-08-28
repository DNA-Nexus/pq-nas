#!/usr/bin/env python3

from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

root = Path(__file__).resolve().parents[2]

main = (
    root /
    "server/src/main.cpp"
).read_text(encoding="utf-8")

route = (
    root /
    "server/src/routes/routes_internal_storage_handoff.cpp"
).read_text(encoding="utf-8")

cmake = (
    root /
    "CMakeLists.txt"
).read_text(encoding="utf-8")

production_tu = read_main_translation_unit_text()

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


require(
    '#include "routes/routes_internal_storage_handoff.h"' in main,
    "production server must include internal Storage handoff route",
)

require(
    '#include "cluster/storage_handoff_ticket_store.h"' in main,
    "production server must include handoff ticket store",
)

require(
    "storage_handoff_ticket_store" in main and
    "StorageHandoffTicketStore" in main,
    "production server must own handoff ticket store",
)

runtime_construct_pos = main.find(
    "storage_user_access_runtime ="
)

ticket_store_construct_pos = main.find(
    "storage_handoff_ticket_store ="
)

registration_pos = main.find(
    "register_internal_storage_handoff_routes("
)

require(
    runtime_construct_pos >= 0 and
    ticket_store_construct_pos > runtime_construct_pos and
    registration_pos > ticket_store_construct_pos,
    "handoff store/route must be composed after Storage authorization runtime",
)

block_start = main.find(
    """if (cluster_config.is_node()) {
        pqnas::cluster::
            InternalStorageHandoffRoutesContext"""
)

block_end = main.find(
    "    if (cluster_config.is_director()) {",
    block_start,
)

handoff_block = (
    main[block_start:block_end]
    if block_start >= 0 and
       block_end > block_start
    else ""
)

require(
    handoff_block,
    "internal handoff production block must be Node-role scoped",
)

require(
    "cluster_credential_runtime." in handoff_block and
    "verify_authorization(" in handoff_block,
    "internal handoff must use rotation-aware Node credential runtime",
)

require(
    """internal_storage_handoff_context.ticket_store =
            storage_handoff_ticket_store.get();""" in handoff_block,
    "internal handoff must use server-lifetime ticket store",
)

require(
    "storage_user_access_runtime->" in handoff_block and
    "session_policy()" in handoff_block and
    "storage_session_policy->" in handoff_block and
    "authorize(" in handoff_block,
    "handoff user authorization must reuse canonical Storage session policy",
)

require(
    "StorageUserSessionPolicyResult::" in handoff_block and
    "Authorized" in handoff_block,
    "handoff must require an Authorized Storage session-policy result",
)

require(
    "storage_user_projection_store.get(" not in handoff_block,
    "production handoff must not duplicate projection authorization policy",
)

require(
    "consume_rate_limit_slot =" not in handoff_block,
    "production must retain the route's bounded default handoff limiter",
)

require(
    production_tu.count(
        "register_internal_storage_handoff_routes("
    ) == 1,
    "internal Storage handoff route must be production-registered exactly once",
)

require(
    "if (!captured.consume_rate_limit_slot)" in route and
    "DefaultStorageHandoffRateLimiter" in route,
    "internal handoff route must install bounded default rate limiter",
)

server_link_pos = cmake.find(
    "target_link_libraries(pqnas_server PRIVATE",
    cmake.find("# Node production composition.")
)

require(
    server_link_pos >= 0 and
    "pqnas_internal_storage_handoff_routes" in
        cmake[server_link_pos:server_link_pos + 600],
    "production server must link internal Storage handoff routes",
)

# This PR opens service-to-service ticket issuance only.
# Browser-facing handoff remains deliberately closed.
require(
    "register_storage_handoff_preauth_routes(" not in production_tu,
    "Storage browser preauth must remain production-disabled",
)

require(
    "register_storage_handoff_consume_routes(" not in production_tu,
    "Storage browser consume must remain production-disabled",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: internal Storage handoff production wiring guards passed")
