#!/usr/bin/env python3

from pathlib import Path

route = Path(
    "server/src/routes/"
    "routes_admin_user_lifecycle.cpp"
).read_text()

registry = Path(
    "server/src/users_registry.cpp"
).read_text()

header = Path(
    "server/src/routes/routes_admin_user_lifecycle.h"
).read_text()

wiring = Path(
    "server/src/routes/routes_files_put.inc"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    "erase_persisted_if_unplaced(" in route,
    "lifecycle delete must use persisted delete primitive",
)

require(
    "ctx.users->erase(fp)" not in route,
    "lifecycle delete must not erase memory separately",
)

require(
    "ctx.users->save(ctx.users_path)" not in route,
    "lifecycle delete must not persist in a second operation",
)

require(
    "cluster_revoke_required" in route,
    "placed-user delete must fail closed until revoke reconciliation exists",
)

require(
    "if (!iterator->second.home_node.empty())" in registry,
    "persisted delete primitive must check placement under UsersRegistry lock",
)

require(
    "by_fp_[previous.fingerprint] =" in registry,
    "failed delete persistence must restore the complete previous record",
)


require(
    "erase_user_persisted_cluster_safe" in header and
    "erase_user_persisted_cluster_safe" in route,
    "lifecycle route must support a Director-safe persisted delete transaction",
)

require(
    "cluster_revoke_pending" in route,
    "Director delete must expose pending Storage revoke separately",
)

require(
    "admin_user_lifecycle_ctx"
    "\n                .erase_user_persisted_cluster_safe" in wiring,
    "Director lifecycle wiring must install cluster-safe delete transaction",
)

require(
    "director_storage_projection_transition_mutex" in main and
    "director_storage_projection_transition_mutex" in wiring,
    "physical delete and reconciliation must share one transition mutex",
)

worker_callback = main.find(
    "director_storage_projection_reconcile_worker_config"
    "\n        .reconcile_once ="
)

worker_lock = main.find(
    "transition_lock(",
    worker_callback,
)

worker_reconcile = main.find(
    ".reconcile_once();",
    worker_lock,
)

require(
    worker_callback >= 0 and
    worker_lock > worker_callback and
    worker_reconcile > worker_lock,
    "production worker must lock transition mutex before reconcile_once",
)

delete_callback = wiring.find(
    ".erase_user_persisted_cluster_safe ="
)

delete_lock = wiring.find(
    "transition_lock(",
    delete_callback,
)

durability_check = wiring.find(
    ".ensure_durable(",
    delete_lock,
)

confirmed_check = wiring.find(
    ".get(fp)",
    durability_check,
)

durable_delete = wiring.find(
    "users.erase_persisted_if_unplaced(",
    confirmed_check,
)

require(
    delete_callback >= 0 and
    delete_lock > delete_callback and
    durability_check > delete_lock and
    confirmed_check > durability_check and
    durable_delete > confirmed_check,
    "Director delete must lock, heal sync-state, check confirmed state, "
    "then durably erase the user",
)

require(
    '"cluster_revoke_pending"' in route,
    "confirmed Storage state must block physical delete with 409",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: persisted user-delete caller guards passed")
