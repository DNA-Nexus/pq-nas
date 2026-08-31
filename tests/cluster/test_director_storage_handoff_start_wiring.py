from pathlib import Path

root = Path(__file__).resolve().parents[2]

main = (
    root /
    "server/src/main.cpp"
).read_text(encoding="utf-8")

cmake = (
    root /
    "CMakeLists.txt"
).read_text(encoding="utf-8")

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


require(
    '#include "routes/routes_director_storage_handoff_start.h"' in main,
    "pqnas_server must include Director handoff start route API",
)

registration_pos = main.find(
    "register_director_storage_handoff_start_routes("
)

require(
    registration_pos >= 0,
    "Director handoff start route must be production-registered",
)

director_block_pos = main.rfind(
    "if (cluster_config.is_director()) {",
    0,
    registration_pos,
)

activity_pos = main.find(
    "pqnas::ActivityRoutesDeps activity_deps;",
    registration_pos,
)

require(
    director_block_pos >= 0 and
    registration_pos > director_block_pos and
    activity_pos > registration_pos,
    "handoff start route must remain inside the Director-only cluster route block",
)

context_pos = main.find(
    "DirectorStorageHandoffStartRoutesContext",
    director_block_pos,
)

context_end = main.find(
    "register_director_storage_handoff_start_routes(",
    context_pos,
)

context_block = (
    main[context_pos:context_end]
    if context_pos >= 0 and context_end > context_pos
    else ""
)

require(
    "require_user_cookie_users_actor(" in context_block,
    "handoff start must use cookie-only user authentication",
)

require(
    "require_user_auth_users_actor(" not in context_block,
    "handoff start must not use mixed Bearer/cookie authentication",
)

require(
    "expected_director_origin =" in context_block and
    "ORIGIN;" in context_block,
    "handoff start must use configured Director origin",
)

require(
    'user->status != "enabled"' in context_block and
    'user->storage_state != "allocated"' in context_block and
    "user->home_node.empty()" in context_block and
    "user->storage_projection_revision == 0" in context_block,
    "handoff start must re-check enabled allocated placement state",
)

require(
    "director_storage_projection_sync_state_store.get(" in context_block and
    "confirmed->home_node != user->home_node" in context_block and
    "confirmed->revision !=" in context_block and
    "user->storage_projection_revision" in context_block,
    "handoff start must require the exact confirmed Storage projection",
)

require(
    "director_node_registry.get(" in context_block and
    "!node->enabled" in context_block and
    "node->api_base_url.empty()" in context_block and
    "node->browser_base_url.empty()" in context_block,
    "handoff start must resolve enabled trusted Node routing",
)

require(
    "director_storage_handoff_ticket_client.get()" in context_block and
    "storage_handoff_ticket_client->issue(" in context_block,
    "handoff start must delegate ticket minting to the hardened production client",
)

require(
    """target_link_libraries(pqnas_server PRIVATE
        pqnas_director_storage_handoff_start_routes
)""" in cmake,
    "pqnas_server must link Director handoff start routes",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: Director storage handoff start production wiring guards passed")
