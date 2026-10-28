from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

root = Path(__file__).resolve().parents[2]

main = (root / "server/src/main.cpp").read_text(
    encoding="utf-8"
)

cmake = (root / "CMakeLists.txt").read_text(
    encoding="utf-8"
)

route = (
    root
    / "server/src/routes/routes_storage_handoff_consume.cpp"
).read_text(
    encoding="utf-8"
)

production_tu = read_main_translation_unit_text()

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


require(
    "register_storage_handoff_consume_routes(" in route,
    "consume route module must define route registration",
)

registration_pos = main.find(
    "register_storage_handoff_consume_routes("
)

require(
    registration_pos >= 0,
    "Storage browser consume must be production-registered",
)

node_block_pos = main.rfind(
    "if (cluster_config.is_node()) {",
    0,
    registration_pos,
)

director_block_pos = main.find(
    "if (cluster_config.is_director()) {",
    registration_pos,
)

require(
    node_block_pos >= 0 and
    registration_pos > node_block_pos and
    director_block_pos > registration_pos,
    "Storage browser consume must remain inside a Storage Node-only block",
)

context_pos = main.rfind(
    "StorageHandoffConsumeRoutesContext",
    node_block_pos,
    registration_pos,
)

context_block = (
    main[context_pos:registration_pos]
    if context_pos >= 0
    else ""
)

require(
    "expected_storage_origin =" in context_block and
    "ORIGIN;" in context_block,
    "Storage consume must use configured server origin",
)

require(
    "storage_handoff_consumer->" in context_block and
    "consume(" in context_block,
    "Storage consume route must delegate to production consume service",
)

require(
    "consume_rate_limit_slot =" not in context_block,
    "Storage consume production wiring must use bounded route default limiter",
)

require(
    "storage_handoff_consume_service_config.ticket_store =" in main and
    "storage_handoff_ticket_store.get();" in main and
    "storage_handoff_consume_service_config.session_policy =" in main and
    "storage_user_access_runtime->" in main and
    "session_policy();" in main,
    "consume service must use Node-local ticket and projection authority",
)

require(
    "storage_handoff_consume_service_config.base_cookie_key =" in main and
    "COOKIE_KEY;" in main and
    "storage_handoff_consume_service_config.local_node_id =" in main and
    "cluster_config.node_id;" in main,
    "consume service must bind sessions to process key and stable Node ID",
)

require(
    "SESS_TTL <= 0" in main and
    "kStorageUserSessionCookieMaxTtlSeconds" in main and
    "storage_handoff_consume_service_config.session_ttl_seconds =" in main,
    "Storage handoff session lifetime must be positive and capped by hard ceiling",
)

require(
    "register_storage_handoff_preauth_routes(" in production_tu and
    "register_storage_handoff_consume_routes(" in production_tu,
    "production translation unit must contain complete Storage browser backend",
)

require(
    """target_link_libraries(pqnas_server PRIVATE
        pqnas_storage_handoff_preauth_routes
        pqnas_storage_handoff_consume_routes
)""" in cmake,
    "pqnas_server must link Storage browser handoff routes",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: Storage handoff consume production wiring guards passed")
