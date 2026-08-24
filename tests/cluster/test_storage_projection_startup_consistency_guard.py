#!/usr/bin/env python3

from pathlib import Path


module = Path(
    "server/src/cluster/storage_projection_startup_consistency.cpp"
).read_text()

header = Path(
    "server/src/cluster/storage_projection_startup_consistency.h"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    "check_storage_projection_startup_consistency(" in header,
    "startup consistency API must be declared",
)

require(
    "confirmed.revision >" in module and
    "storage_projection_revision" in module,
    "startup consistency must detect confirmed revision ahead of users.json",
)

require(
    "confirmed_revision_ahead" in module,
    "startup consistency must report aggregate confirmed rollback count",
)

require(
    "pending.revision >" in module and
    "pending_revision_ahead" in module,
    "startup consistency must detect pending revision ahead of users.json",
)


director_start = main.find(
    "if (cluster_config.is_director())"
)

client_start = main.find(
    "StorageUserProjectionSyncClientConfig",
    director_start,
)

require(
    director_start >= 0 and
    client_start > director_start,
    "Director startup composition block must be present",
)

director_block = (
    main[director_start:client_start]
    if director_start >= 0 and
       client_start > director_start
    else ""
)

call = (
    "check_storage_projection_startup_consistency("
)

require(
    call in director_block,
    "startup consistency check must be production-wired Director-only",
)

confirmed_load_pos = director_block.find(
    "director_storage_projection_sync_state_store.load("
)

pending_load_pos = director_block.find(
    "director_storage_projection_transition_store.load("
)

check_pos = director_block.find(
    call
)

require(
    confirmed_load_pos >= 0,
    "Director must load confirmed state before consistency check",
)

require(
    pending_load_pos >= 0,
    "Director must load pending journal before consistency check",
)

require(
    check_pos >= 0,
    "Director consistency call must exist",
)

if (
    confirmed_load_pos >= 0 and
    pending_load_pos >= 0 and
    check_pos >= 0
):
    require(
        confirmed_load_pos < check_pos and
        pending_load_pos < check_pos,
        "startup consistency must run after both durable-state loads",
    )

require(
    "confirmed_revision_ahead" in director_block and
    "pending_revision_ahead" in director_block and
    "return 3;" in director_block,
    "newer confirmed or pending state must fail Director startup closed",
)


if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection startup consistency guards passed")
