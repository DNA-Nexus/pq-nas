#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/users_registry.cpp"
).read_text()

header = Path(
    "server/src/users_registry.h"
).read_text()

reconciler = Path(
    "server/src/cluster/storage_user_projection_reconciler.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


# ---------------------------------------------------------------------------
# users.json durable atomic replace
# ---------------------------------------------------------------------------

require(
    "std::ofstream" not in source,
    "users.json atomic save must not use std::ofstream",
)

require(
    "O_EXCL" in source and
    "O_NOFOLLOW" in source,
    "users.json temp file must use exclusive no-follow creation",
)

require(
    "::renameat(" in source,
    "users.json persistence must use atomic renameat",
)

require(
    source.count("::fsync(") >= 2,
    "users.json persistence must fsync file data and parent directory",
)

require(
    "*committed = true;" in source,
    "users.json save must report a rename that already became visible",
)

require(
    "durability_uncertain_ = true;" in source,
    "users.json must mark post-rename durability uncertainty",
)

require(
    "durability_uncertain_ = false;" in source,
    "successful directory fsync must clear users.json uncertainty",
)


# ---------------------------------------------------------------------------
# Uncertain committed state must not be rolled back.
# ---------------------------------------------------------------------------

require(
    "bool* committed" in header,
    "save_locked must distinguish visible commit from pre-commit failure",
)

require(
    "bool durability_uncertain_" in header,
    "UsersRegistry must retain durability uncertainty",
)

require(
    "bool ensure_durable(" in header,
    "UsersRegistry must expose current-snapshot durability healing",
)

require(
    "ensure_durable_locked(" in source,
    "UsersRegistry must implement locked durability healing",
)

persisted_functions = [
    "upsert_persisted",
    "set_status_persisted",
    "set_storage_metadata_persisted",
    "switch_storage_mapping_persisted_if_pool_current",
    "set_home_node_persisted",
    "set_home_node_persisted_if_current",
    "clear_home_node_persisted_if_current",
    "set_placement_policy_id_persisted",
    "erase_persisted_if_unplaced",
]

require(
    source.count(
        "bool registry_commit_visible = false;"
    ) == len(persisted_functions),
    "every persisted UsersRegistry mutation must track rename visibility",
)

require(
    source.count(
        "if (registry_commit_visible)"
    ) == len(persisted_functions),
    "every persisted mutation must avoid rollback after committed rename",
)


for name in persisted_functions:
    marker = f"bool UsersRegistry::{name}("
    start = source.find(marker)

    require(
        start >= 0,
        f"persisted UsersRegistry function exists: {name}",
    )

    if start < 0:
        continue

    next_candidates = [
        pos
        for token in (
            "\nbool UsersRegistry::",
            "\nstd::unordered_map<",
            "\nvoid UsersRegistry::",
        )
        if (pos := source.find(
            token,
            start + len(marker)
        )) >= 0
    ]

    end = (
        min(next_candidates)
        if next_candidates
        else len(source)
    )

    body = source[start:end]

    require(
        "ensure_durable_locked(error)" in body,
        f"{name} must heal prior durability uncertainty before success",
    )

    require(
        "bool registry_commit_visible = false;" in body,
        f"{name} must distinguish pre/post-rename persistence failure",
    )


# ---------------------------------------------------------------------------
# No Node transition from uncertain authoritative Director state.
# ---------------------------------------------------------------------------

users_gate = reconciler.find(
    "config_.users->ensure_durable("
)

state_gate = reconciler.find(
    "config_.state_store->ensure_durable("
)

snapshot = reconciler.find(
    "config_.users->snapshot()"
)

require(
    users_gate >= 0,
    "reconciler must ensure users.json durability",
)

require(
    state_gate >= 0,
    "reconciler must ensure confirmed-state durability",
)

require(
    snapshot >= 0,
    "reconciler users snapshot anchor must exist",
)

if users_gate >= 0 and state_gate >= 0:
    require(
        users_gate < state_gate,
        "authoritative users.json durability must be checked first",
    )

if users_gate >= 0 and snapshot >= 0:
    require(
        users_gate < snapshot,
        "users.json durability gate must precede reconciliation snapshot",
    )


if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: UsersRegistry durability guards passed")
