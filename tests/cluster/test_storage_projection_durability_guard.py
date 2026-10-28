#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/cluster/storage_user_projection_store.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    '''if (committed) {
        projections_ =
            std::move(candidate);

        durability_uncertain_ =
            !durable;
    }''' in source,
    "durability uncertainty must only change after a committed rename",
)

require(
    '''durability_uncertain_ =
        committed && !durable;''' not in source,
    "pre-commit persistence failure must not clear earlier uncertainty",
)

require(
    "if (!durability_uncertain_)" in source,
    "identical revision retry must still consult durability uncertainty",
)

require(
    "commit_candidate_locked(" in source,
    "uncertain identical revision must still retry persistence",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection durability uncertainty guards passed")
