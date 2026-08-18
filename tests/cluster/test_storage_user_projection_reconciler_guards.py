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

# Scope A->B ordering to the placed-user move branch. Explicit unplacement
# now has its own earlier tombstone and must not weaken this invariant.
move_branch = source.find(
    "// confirmed revision is older than current Director revision."
)

move_tombstone = source.find(
    "const StorageUserProjection tombstone =",
    move_branch,
)

revoke_pos = source.find(
    "config_.sync_projection(\n"
    "                        tombstone",
    move_tombstone,
)

current_pos = source.find(
    "config_.sync_projection(\n"
    "                        current",
    revoke_pos + 1 if revoke_pos >= 0 else move_tombstone,
)

require(
    move_branch >= 0 and
    move_tombstone >= move_branch and
    revoke_pos >= move_tombstone and
    current_pos > revoke_pos,
    "old Node must be revoked before current Node is synchronized",
)

# Explicit A->empty ordering:
# durable Director revision -> newer tombstone to the last confirmed Node ->
# CAS erase of that exact confirmed state.
unplaced_branch = source.find(
    "if (user.home_node.empty())"
)

unplaced_tombstone = source.find(
    "unplaced_user_tombstone(",
    unplaced_branch,
)

unplaced_revoke = source.find(
    "config_.sync_projection(\n"
    "                        tombstone",
    unplaced_tombstone,
)

unplaced_erase = source.find(
    "erase_if_current(",
    unplaced_revoke,
)

placed_projection_build = source.find(
    "StorageUserProjection current;",
    unplaced_branch,
)

require(
    unplaced_branch >= 0 and
    unplaced_tombstone >= unplaced_branch and
    unplaced_revoke >= unplaced_tombstone and
    unplaced_erase > unplaced_revoke and
    (
        placed_projection_build < 0 or
        unplaced_erase < placed_projection_build
    ),
    "unplacement must revoke the last confirmed Node before erasing sync-state",
)

require(
    "confirmed->revision >\n"
    "                user.storage_projection_revision" in source,
    "unplacement must reject newer confirmed revisions",
)

require(
    "confirmed->revision ==\n"
    "                user.storage_projection_revision" in source,
    "unplacement must fail closed on ambiguous equal revisions",
)

require(
    "++result.revoked_unplaced;" in source,
    "successful explicit unplacement must be surfaced separately",
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
