#!/usr/bin/env python3

from pathlib import Path

allocation = Path(
    "server/src/routes/routes_admin_user_storage.cpp"
).read_text()

migration = Path(
    "server/src/user_storage_migration.cpp"
).read_text()

registry_h = Path(
    "server/src/users_registry.h"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    "c.users->set_storage_metadata_persisted(" in allocation,
    "admin storage allocation must use write-through storage mutation",
)

require(
    "const bool ok_upsert = c.users->upsert(u);" not in allocation,
    "admin storage allocation must not use upsert + later save",
)

require(
    "switch_storage_mapping_persisted_if_pool_current(" in migration,
    "storage migration must use locked mapping CAS",
)

require(
    "if (!users.upsert(u))" not in migration,
    "storage migration must not use generic upsert",
)

require(
    "users.save(users_path)" not in migration,
    "storage migration must not split mutation from persistence",
)

require(
    "switch_storage_mapping_persisted_if_pool_current(" in registry_h,
    "UsersRegistry must expose narrow migration mapping CAS",
)

require(
    "set_storage_metadata_persisted_if_pool_current(" not in registry_h,
    "old full-storage-snapshot migration CAS must stay removed",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage metadata persisted-caller guards passed")
