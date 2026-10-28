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
    / "server/src/routes/routes_storage_handoff_preauth.cpp"
).read_text(
    encoding="utf-8"
)

production_tu = read_main_translation_unit_text()

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


require(
    '"/api/storage/handoff/preauth"' in route,
    "preauth route module must define browser preauth endpoint",
)

registration_pos = main.find(
    "register_storage_handoff_preauth_routes("
)

require(
    registration_pos >= 0,
    "Storage browser preauth must be production-registered",
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
    "Storage browser preauth must remain inside a Storage Node-only block",
)

context_pos = main.rfind(
    "StorageHandoffPreauthRoutesContext",
    node_block_pos,
    registration_pos,
)

context_block = (
    main[context_pos:registration_pos]
    if context_pos >= 0
    else ""
)

require(
    "expected_origin =" in context_block and
    "ORIGIN;" in context_block,
    "Storage preauth must use configured server origin",
)

require(
    "director_browser_origin =" in context_block and
    "cluster_config.director_url;" in context_block,
    "Storage preauth must return only trusted configured Director authority",
)

require(
    "consume_rate_limit_slot =" not in context_block,
    "Storage preauth production wiring must use bounded route default limiter",
)

require(
    "register_storage_handoff_preauth_routes(" in production_tu,
    "production translation unit must contain Storage preauth registration",
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

print("OK: Storage handoff preauth production wiring guards passed")
