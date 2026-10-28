#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/cluster/storage_user_projection_sync_state_store.cpp"
).read_text()

header = Path(
    "server/src/cluster/storage_user_projection_sync_state_store.h"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

reconciler = Path(
    "server/src/cluster/storage_user_projection_reconciler.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


# ---------------------------------------------------------------------------
# Filesystem / durability hardening
# ---------------------------------------------------------------------------

require(
    "O_NOFOLLOW" in source,
    "sync-state filesystem access must reject symlink targets",
)

require(
    "O_EXCL" in source,
    "sync-state temp files must use exclusive creation",
)

require(
    "::fsync(" in source,
    "sync-state persistence must fsync data and parent directory",
)

require(
    "::renameat(" in source,
    "sync-state persistence must atomically rename the temp file",
)

require(
    "(stats.st_mode & 0077) != 0" in source,
    "existing sync-state file must require private permissions",
)

require(
    '''if (committed) {
        states_ =
            std::move(candidate);

        durability_uncertain_ =
            !durable;
    }''' in source,
    "durability uncertainty must change only after committed rename",
)

require(
    "if (!durability_uncertain_)" in source,
    "identical state retry must preserve durability recovery",
)

ensure_start = source.find(
    "StorageUserProjectionSyncStateStore::\nensure_durable("
)

ensure_end = source.find(
    "StorageUserProjectionSyncStateStore::mark_synced(",
    ensure_start,
)

ensure_body = (
    source[ensure_start:ensure_end]
    if ensure_start >= 0 and
    ensure_end > ensure_start
    else ""
)

require(
    "bool ensure_durable(" in header,
    "sync-state store must expose current-snapshot durability healing",
)

require(
    bool(ensure_body),
    "sync-state ensure_durable implementation must be present",
)

require(
    "if (!durability_uncertain_)" in ensure_body,
    "ensure_durable must avoid unnecessary rewrites of durable state",
)

require(
    "states_" in ensure_body and
    "commit_candidate_locked(" in ensure_body,
    "ensure_durable must re-persist the exact current snapshot",
)

require(
    "state_store->ensure_durable(" in reconciler,
    "reconciler must heal confirmed-state durability before transitions",
)


# ---------------------------------------------------------------------------
# Minimal persisted schema
# ---------------------------------------------------------------------------

require(
    "std::string fingerprint;" in header and
    "std::string home_node;" in header and
    "std::uint64_t revision = 0;" in header,
    "sync-state must contain the identity/Node/revision tuple",
)

# Inspect only the persisted struct. Security comments intentionally mention
# credentials/authorization while explaining what is NOT stored.
struct_start = header.find(
    "struct StorageUserProjectionSyncState {"
)

struct_end = header.find(
    "};",
    struct_start,
)

require(
    struct_start >= 0 and
    struct_end > struct_start,
    "sync-state struct must be present",
)

state_struct = (
    header[struct_start:struct_end + 2]
    if struct_start >= 0 and
    struct_end > struct_start
    else ""
)

for forbidden in (
    "quota_bytes",
    "root_rel",
    "storage_pool_id",
    "authorization",
    "credential",
    "password",
):
    require(
        forbidden not in state_struct,
        f"sync-state struct must not persist {forbidden}",
    )


# ---------------------------------------------------------------------------
# Production phase
# ---------------------------------------------------------------------------

require(
    "StorageUserProjectionSyncStateStore" in main,
    "Director must production-wire persistent projection sync-state",
)

require(
    '"storage_projection_sync_state.json"' in main,
    "Director sync-state must use its fixed config-root filename",
)

require(
    "director_storage_projection_sync_state_store.load(" in main,
    "Director must load persistent projection sync-state before reconciliation",
)

require(
    "sync-state file is missing or inaccessible" in main,
    "production Director must not silently bootstrap missing sync-state",
)


if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection sync-state store structural guards passed")
