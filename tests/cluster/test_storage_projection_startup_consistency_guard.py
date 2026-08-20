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
    "startup consistency must report aggregate rollback count",
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

load_pos = director_block.find(
    "director_storage_projection_sync_state_store.load("
)

check_pos = director_block.find(
    call
)

require(
    load_pos >= 0,
    "Director must load confirmed state before consistency check",
)

require(
    check_pos >= 0,
    "Director consistency call must exist",
)

if load_pos >= 0 and check_pos >= 0:
    require(
        load_pos < check_pos,
        "startup consistency check must run after confirmed-state load",
    )

require(
    "confirmed_revision_ahead" in director_block and
    "return 3;" in director_block,
    "newer confirmed state must fail Director startup closed",
)


if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection startup consistency guards passed")
