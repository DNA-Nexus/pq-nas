#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/cluster/"
    "storage_user_projection_reconciler.cpp"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    "config_.users->snapshot()" in source,
    "reconciler must operate from a stable Director user snapshot",
)

require(
    "previous_node_tombstone(" in source,
    "placement reconciliation must construct an old-Node tombstone",
)

require(
    "tombstone.enabled = false;" in source,
    "old-Node tombstone must disable authorization",
)

require(
    "tombstone.storage_allocated = false;" in source,
    "old-Node tombstone must remove storage allocation",
)

# Scope the ordering check to the placement-move branch. There are legitimate
# sync_projection(current) calls earlier in the file for first placement and
# same-Node revision updates.
move_branch = source.find(
    "const StorageUserProjection tombstone ="
)

revoke_pos = source.find(
    "config_.sync_projection(\n"
    "                        tombstone",
    move_branch,
)

current_pos = source.find(
    "config_.sync_projection(\n"
    "                        current",
    revoke_pos + 1 if revoke_pos >= 0 else move_branch,
)

require(
    move_branch >= 0 and
    revoke_pos >= move_branch and
    current_pos > revoke_pos,
    "old Node must be revoked before current Node is synchronized",
)

require(
    "confirmed->revision >\n            current.revision" in source,
    "newer confirmed state must block Director rollback",
)

require(
    "confirmed->revision ==\n            current.revision" in source,
    "same-revision state must be explicitly checked",
)

require(
    "confirmed->home_node !=\n                current.home_node" in source,
    "same-revision different-Node state must fail closed",
)

require(
    "store->mark_synced(" in source,
    "network success must be followed by persistent confirmation",
)

require(
    "orphaned_states" in source,
    "orphaned persistent state must be surfaced, not silently deleted",
)

# Still foundation-only. Worker/thread and production wiring come later.


# Production phase: deterministic reconciler is now intentionally Director-wired.
require(
    "StorageUserProjectionReconciler" in main,
    "Director must production-wire the deterministic reconciler",
)

require(
    "director_storage_projection_reconciler" in main,
    "production worker must delegate through the Director reconciler",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection reconciler structural guards passed")
