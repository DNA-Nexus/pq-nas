#!/usr/bin/env python3

from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

main = Path("server/src/main.cpp").read_text()
cmake = Path("CMakeLists.txt").read_text()
projection_route = Path(
    "server/src/routes/routes_internal_storage_user_projection.cpp"
).read_text()

# Security: negative production-wiring assertions must inspect the
# implementation .inc files compiled into main.cpp as well as the
# literal main.cpp source. This closes historical RT-011.
production_tu = read_main_translation_unit_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    '#include "cluster/storage_user_projection_store.h"' in main,
    "main includes StorageUserProjectionStore",
)

require(
    '#include "routes/routes_internal_storage_user_projection.h"' in main,
    "main includes internal projection route",
)

require(
    'StorageUserProjectionStore\n        storage_user_projection_store;' in main,
    "projection store has process lifetime",
)

require(
    'pqnas::config_root_path() /\n                "storage_user_projections.json"' in main,
    "projection store path is fixed below config_root_path",
)

require(
    'if (!storage_user_projection_store.load(' in main,
    "Node startup checks projection-store load result",
)

load_pos = main.find(
    "if (!storage_user_projection_store.load("
)

route_pos = main.find(
    "register_internal_storage_user_projection_routes("
)

require(
    load_pos >= 0 and route_pos >= 0 and load_pos < route_pos,
    "projection store loads before route registration",
)

require(
    'internal_storage_user_projection_context.local_node_id =\n'
    '            cluster_config.node_id;' in main,
    "route binds projections to configured Node ID",
)

require(
    "register_internal_storage_user_projection_routes(" in main,
    "projection sync route is registered",
)

require(
    '"/internal/storage/user-projection/audit"' in projection_route and
    "handle_internal_storage_user_projection_audit_request(" in projection_route,
    "RT-008 projection audit endpoint is registered in the production route module",
)

# Security architecture invariant:
# handoff stays disabled until Director projection/revision synchronization is
# complete and authorization is wired to this projection store.
require(
    "register_internal_storage_handoff_routes(" not in production_tu,
    "handoff ticket route is still not production-registered",
)

# RT010: physical Node deletion must share the exact durable confirmed-state
# store and transition boundary used by reconciliation.
require(
    "director_node_deletion_service_config.projection_sync_state_store =\n"
    "        &director_storage_projection_sync_state_store;" in main,
    "Node deletion must inspect Director confirmed projection sync-state",
)

require(
    "director_node_deletion_service_config.projection_transition_mutex =\n"
    "        director_storage_projection_transition_mutex;" in main,
    "Node deletion must share the reconciliation transition mutex",
)

require(
    "pqnas_node_deletion_service PUBLIC\n"
    "        pqnas_storage_user_projection_sync_state_store" in cmake,
    "Node deletion service must link durable projection sync-state support",
)


# Post-commit projection changes must wake the running Director reconciler.
worker_start_pos = main.find(
    "if (!director_storage_projection_reconcile_worker.start("
)

notifier_bind_pos = main.find(
    "users.set_storage_projection_commit_notifier("
)

require(
    worker_start_pos >= 0 and
    notifier_bind_pos >= 0 and
    worker_start_pos < notifier_bind_pos,
    "Director binds projection commit notifier only after worker start",
)

require(
    "const pqnas::StorageProjectionCommitInfo& info" in main and
    'audit.event =\n                        "cluster.storage_projection.committed";' in main and
    'audit.f["previous_revision"]' in main and
    'audit.f["projection_revision"]' in main and
    'audit.f["previous_home_node"]' in main and
    'audit.f["home_node"]' in main and
    "(void)director_storage_projection_reconcile_worker.wake();" in main,
    "projection commit notifier audits exact revision transition and wakes Director reconciliation worker",
)

shutdown_notifier_clear_pos = main.rfind(
    "users.set_storage_projection_commit_notifier({});"
)

worker_stop_pos = main.rfind(
    "director_storage_projection_reconcile_worker.stop_and_join();"
)

require(
    shutdown_notifier_clear_pos >= 0 and
    worker_stop_pos >= 0 and
    shutdown_notifier_clear_pos < worker_stop_pos,
    "projection notifier is detached before Director worker shutdown",
)

require(
    "add_library(pqnas_storage_path_validation STATIC" in cmake,
    "path validation is a dedicated library",
)

require(
    "pqnas_storage_user_projection_store PUBLIC\n"
    "        pqnas_cluster_identity_validation\n"
    "        pqnas_storage_path_validation" in cmake,
    "projection store explicitly depends on path validation",
)

require(
    "pqnas_internal_storage_user_projection_routes PUBLIC\n"
    "        pqnas_storage_user_projection_store\n"
    "        pqnas_storage_user_projection_audit" in cmake,
    "projection route explicitly links RT-008 audit primitives",
)

require(
    "pqnas_server PRIVATE\n"
    "        pqnas_storage_path_validation\n"
    "        pqnas_internal_storage_user_projection_routes" in cmake,
    "pqnas_server links projection production dependencies",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage user projection production wiring guards passed")
