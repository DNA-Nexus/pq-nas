#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/cluster/storage_user_projection_sync_service.cpp"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    "previous_node_tombstone(" in source,
    "placement sync must construct a previous-Node tombstone",
)

require(
    "tombstone.enabled = false;" in source,
    "previous Node tombstone must disable authorization",
)

require(
    "tombstone.storage_allocated = false;" in source,
    "previous Node tombstone must remove storage allocation",
)

require(
    "tombstone.revision =\n        current.revision;" in source,
    "previous and current Node writes must share the new Director revision",
)

revoke_position = source.find(
    "config_.sync_projection(\n                    tombstone"
)

current_position = source.find(
    "config_.sync_projection(\n                current"
)

require(
    revoke_position >= 0 and
    current_position >= 0 and
    revoke_position < current_position,
    "previous Node must be revoked before current Node is synchronized",
)

require(
    "PreviousNodeRevokeFailed" in source,
    "failed previous-Node revoke must be explicit",
)

# Still no production wiring. First validate this transition policy in
# isolation; persistent retry/reconciliation is a separate step.
require(
    "StorageUserProjectionSyncService" not in main,
    "transition-aware sync service must not yet be production-wired",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection sync service structural guards passed")
