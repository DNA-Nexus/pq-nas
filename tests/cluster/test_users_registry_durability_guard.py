#!/usr/bin/env python3

from pathlib import Path
import re

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

# NEW-007: discover persisted mutation APIs instead of maintaining a brittle
# numeric census. Any newly added persisted UsersRegistry bool method is
# automatically brought under the durability assertions below.
header_persisted_functions = set(
    re.findall(
        r"\bbool\s+"
        r"([A-Za-z_][A-Za-z0-9_]*persisted[A-Za-z0-9_]*)"
        r"\s*\(",
        header,
    )
)

source_bool_definitions = list(
    re.finditer(
        r"\nbool\s+UsersRegistry::\s*"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*\(",
        source,
    )
)

source_persisted_functions = {
    match.group(1)
    for match in source_bool_definitions
    if "persisted" in match.group(1)
}

require(
    bool(header_persisted_functions),
    "persisted UsersRegistry mutation API census must not be empty",
)

require(
    header_persisted_functions ==
        source_persisted_functions,
    "persisted UsersRegistry declarations and definitions must match",
)

definition_by_name = {
    match.group(1): (index, match)
    for index, match in enumerate(
        source_bool_definitions
    )
}

for name in sorted(header_persisted_functions):
    definition = definition_by_name.get(name)

    require(
        definition is not None,
        f"persisted UsersRegistry function exists: {name}",
    )

    if definition is None:
        continue

    index, match = definition

    end = (
        source_bool_definitions[index + 1].start()
        if index + 1 < len(source_bool_definitions)
        else len(source)
    )

    body = source[
        match.start():
        end
    ]

    require(
        "ensure_durable_locked(error)" in body,
        f"{name} must heal prior durability uncertainty before success",
    )

    require(
        "bool registry_commit_visible = false;" in body,
        f"{name} must distinguish pre/post-rename persistence failure",
    )

    require(
        "if (registry_commit_visible)" in body,
        f"{name} must not roll back after a visible rename commit",
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
