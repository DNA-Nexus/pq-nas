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

production_user_mutation_sources = [
    Path("server/src/main.cpp"),
]

routes_root = Path("server/src/routes")

for pattern in ("*.cpp", "*.cc", "*.inc"):
    production_user_mutation_sources.extend(
        sorted(routes_root.rglob(pattern))
    )


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

# Security: NEW-002 was reachable whenever a route copied a full UserRec,
# called generic upsert(), then saved it after a concurrent authorization
# mutation. Production routes must use narrow/write-through persisted APIs.
for source_path in production_user_mutation_sources:
    source = source_path.read_text()

    require(
        "users->upsert(" not in source and
        "users.upsert(" not in source,
        (
            f"{source_path}: generic UsersRegistry upsert must not be used "
            "in production routes"
        ),
    )

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage metadata persisted-caller guards passed")
