#!/usr/bin/env python3

from pathlib import Path

route = Path(
    "server/src/routes/"
    "routes_admin_user_lifecycle.cpp"
).read_text()

registry = Path(
    "server/src/users_registry.cpp"
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

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: persisted user-delete caller guards passed")
