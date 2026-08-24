#!/usr/bin/env python3

from pathlib import Path
import re

from production_translation_unit import (
    read_main_translation_unit_text,
)

main = Path(
    "server/src/main.cpp"
).read_text()

cmake = Path(
    "CMakeLists.txt"
).read_text()

# Security: negative production-wiring assertions must inspect the
# implementation .inc files compiled into main.cpp as well as the
# literal main.cpp source. This closes historical RT-011.
production_tu = read_main_translation_unit_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


# Direct production dependencies.
for header in (
    "storage_user_projection_sync_state_store.h",
    "storage_user_projection_sync_client.h",
    "storage_user_projection_sync_https.h",
    "storage_user_projection_audit_client.h",
    "storage_user_projection_audit_https.h",
    "storage_user_projection_reconciler.h",
    "storage_user_projection_reconcile_worker.h",
):
    require(
        header in main,
        f"main.cpp must directly include {header}",
    )

require(
    '"storage_projection_sync_state.json"' in main,
    "Director sync-state must use a fixed config-root filename",
)

require(
    "director_storage_projection_sync_state_store.load(" in main,
    "Director must load persistent projection sync-state",
)

# Security: unlike the reusable store API, production Director may not treat a
# missing state file as an empty first-run state. Losing this file could lose
# the previous Node needed for a revoke.
require(
    "std::filesystem::symlink_status(" in main and
    "sync-state file is missing or inaccessible" in main,
    "production must fail closed if projection sync-state is missing",
)

require(
    "make_https_storage_user_projection_sync_transport()" in main,
    "Director production sync must use hardened HTTPS transport",
)

require(
    "make_https_storage_user_projection_audit_transport()" in main,
    "Director production audit must use hardened HTTPS transport",
)

require(
    ".node_registry =" in main and
    "&director_node_registry" in main,
    "projection sync client must use Director NodeRegistry",
)

require(
    ".credential_registry =" in main and
    "&director_node_credential_registry" in main,
    "projection clients must use Director per-node credentials",
)

require(
    "director_storage_projection_audit_client_config" in main and
    ".tls_trust_store =" in main and
    "&director_node_tls_trust_store" in main,
    "projection audit client must use Director SPKI trust store",
)

require(
    "director_storage_projection_reconciler_config.users =" in main and
    "&users;" in main,
    "reconciler must use the loaded Director UsersRegistry",
)

require(
    "director_storage_projection_reconciler_config.state_store =" in main,
    "reconciler must use persistent confirmed sync-state",
)

require(
    "director_storage_projection_sync_client.sync(" in main,
    "reconciler mutation callback must delegate to production sync client",
)

require(
    "director_storage_projection_reconciler_config"
    "\n        .list_audit_capable_nodes =" in main and
    "director_node_registry.snapshot()" in main and
    "node_status_protocol_supports_projection_audit(" in main and
    "node.protocol_version" in main,
    "reconciler audit capability must come from recorded v2 heartbeat protocol",
)

require(
    "director_storage_projection_reconciler_config"
    "\n        .audit_projection_range =" in main and
    "director_storage_projection_audit_client.audit(" in main,
    "reconciler audit callback must delegate to production audit client",
)

require(
    "director_storage_projection_reconciler.reconcile_once()" in main or
    "director_storage_projection_reconciler\n"
    "                        .reconcile_once()" in main,
    "worker callback must delegate to deterministic reconciler",
)

require(
    "director_storage_projection_reconcile_worker.start(" in main,
    "Director must start projection reconciliation worker",
)

require(
    "interval_s=30" in main,
    "production log must document 30-second fallback interval",
)

users_load_pos = main.find(
    "if (!users.load(users_path))"
)

state_load_pos = main.find(
    "director_storage_projection_sync_state_store.load("
)

worker_start_pos = main.find(
    "director_storage_projection_reconcile_worker.start("
)

require(
    users_load_pos >= 0 and
    state_load_pos > users_load_pos and
    worker_start_pos > state_load_pos,
    "projection state/worker must start only after users.json is loaded",
)

projection_stop_pos = main.find(
    "director_storage_projection_reconcile_worker.stop_and_join();"
)

heartbeat_stop_pos = main.find(
    "director_node_heartbeat_worker.stop_and_join();"
)

require(
    projection_stop_pos >= 0 and
    heartbeat_stop_pos > projection_stop_pos,
    "projection reconciliation must stop before heartbeat shutdown",
)

# Security boundary remains closed: this phase synchronizes projections only.
require(
    "register_internal_storage_handoff_routes(" not in production_tu,
    "Storage handoff route must remain unregistered",
)

pqnas_server_link_blocks = re.findall(
    r"target_link_libraries\(pqnas_server PRIVATE\n.*?\n\)",
    cmake,
    flags=re.DOTALL,
)

projection_link_block = next(
    (
        block
        for block in pqnas_server_link_blocks
        if (
            "pqnas_storage_user_projection_sync_https"
                in block and
            "pqnas_storage_user_projection_reconcile_worker"
                in block
        )
    ),
    "",
)

require(
    projection_link_block and
    "pqnas_storage_user_projection_audit_client"
        in projection_link_block and
    "pqnas_storage_user_projection_audit_https"
        in projection_link_block,
    "pqnas_server must link projection sync, audit client/HTTPS and worker",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: Director storage projection reconciliation production wiring guards passed")
