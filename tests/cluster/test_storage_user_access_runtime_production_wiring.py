from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

root = Path(__file__).resolve().parents[2]

main = (root / "server/src/main.cpp").read_text(
    encoding="utf-8"
)

runtime = (
    root /
    "server/src/cluster/storage_user_access_runtime.cpp"
).read_text(
    encoding="utf-8"
)

cmake = (root / "CMakeLists.txt").read_text(
    encoding="utf-8"
)

# Security: negative production-wiring assertions must inspect the
# implementation .inc files compiled into main.cpp as well as the
# literal main.cpp source. This closes historical RT-011.
production_tu = read_main_translation_unit_text()

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


require(
    '#include "cluster/storage_user_access_runtime.h"' in main,
    "production server must include Storage user access runtime",
)

require(
    "StorageUserAccessRuntime" in main and
    "storage_user_access_runtime" in main,
    "production server must own Storage user access runtime",
)

node_composition = """if (cluster_config.is_node()) {
        pqnas::cluster::
            StorageUserAccessRuntimeConfig
                storage_user_access_runtime_config;"""

require(
    node_composition in main,
    "Storage user access runtime must be composed only in Node role",
)

require(
    """storage_user_access_runtime_config.projections =
            &storage_user_projection_store;""" in main,
    "runtime must use the Node-local projection store",
)

require(
    """storage_user_access_runtime_config.base_cookie_key =
            COOKIE_KEY;""" in main,
    "runtime must use the loaded base cookie key",
)

require(
    """storage_user_access_runtime_config.local_node_id =
            cluster_config.node_id;""" in main,
    "runtime must bind sessions to the configured stable Node ID",
)

require(
    "resolve_storage_pool_data_root_strict(" in main,
    "runtime must use strict Storage pool resolution",
)

require(
    "storage_user_access_runtime_config.resolve_pool_data_root" in main,
    "runtime must explicitly wire the Storage pool resolver",
)

require(
    "storage_user_access_runtime_config.clock" in main and
    "pqnas::now_epoch()" in main,
    "runtime must use the production epoch clock",
)

pool_restore_pos = main.find(
    "pool_mounts_restore_managed(users_path);"
)

runtime_construct_pos = main.find(
    "storage_user_access_runtime ="
)

require(
    pool_restore_pos >= 0 and
    runtime_construct_pos > pool_restore_pos,
    "runtime must be constructed after the runtime pool registry is restored",
)

cookie_load_pos = main.find(
    'load_env_key("PQNAS_COOKIE_KEY_B64URL", COOKIE_KEY, 32)'
)

require(
    cookie_load_pos >= 0 and
    runtime_construct_pos > cookie_load_pos,
    "runtime must be constructed after the cookie key is loaded",
)

require(
    "data_root_for_pool_id(" not in runtime,
    "Storage access runtime must never use Director pool fallback wrapper",
)

require(
    "UsersRegistry" not in runtime and
    "user_dir_for_fp" not in runtime,
    "Storage access runtime must remain independent from Director users registry",
)

require(
    "pqnas_storage_user_access_runtime" in cmake,
    "production build must define Storage user access runtime library",
)

server_link_pos = cmake.find(
    "target_link_libraries(pqnas_server PRIVATE",
    cmake.find("# Node production composition.")
)

require(
    server_link_pos >= 0 and
    "pqnas_storage_user_access_runtime" in
        cmake[server_link_pos:server_link_pos + 500],
    "production server must link Storage user access runtime",
)

# Security architecture invariant: composing authorization primitives must not
# implicitly open the browser handoff trust boundary.
require(
    "register_storage_handoff_consume_routes(" not in production_tu,
    "Storage handoff consume must remain production-disabled",
)

require(
    "register_storage_handoff_preauth_routes(" not in production_tu,
    "Storage handoff preauth must remain production-disabled",
)

require(
    "register_internal_storage_handoff_routes(" not in production_tu,
    "internal Storage handoff ticket route must remain production-disabled",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: Storage user access runtime production wiring guards passed")
