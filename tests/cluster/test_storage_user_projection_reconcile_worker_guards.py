#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/cluster/"
    "storage_user_projection_reconcile_worker.cpp"
).read_text()

header = Path(
    "server/src/cluster/"
    "storage_user_projection_reconcile_worker.h"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    "config_.reconcile_once()" in source,
    "worker must delegate reconciliation policy to deterministic reconciler",
)

require(
    "wake_condition_.wait_for(" in source,
    "worker must use interruptible timed waiting",
)

require(
    "std::this_thread::sleep_for" not in source,
    "worker must not use uninterruptible sleeps",
)

require(
    "wake_generation_" in header and
    "wake_generation_" in source,
    "worker must retain wake requests with a generation counter",
)

require(
    "wake_generation_ !=" in source,
    "wake arriving during reconciliation must cause another round",
)

require(
    "catch (...)" in source,
    "one reconciliation exception must not terminate future retries",
)

require(
    "stop_and_join()" in source,
    "worker destructor/lifecycle must provide joined shutdown",
)

require(
    "std::chrono::seconds(30)" in header,
    "worker must have a bounded 30-second fallback reconciliation interval",
)

# Worker remains unconnected to production until the state store, client,
# reconciler and lifecycle are composed together in one Director-only step.


# Production phase: reconciliation worker is now intentionally Director-wired.
require(
    "StorageUserProjectionReconcileWorker" in main,
    "Director must production-wire the reconciliation worker",
)

require(
    "director_storage_projection_reconcile_worker.start(" in main,
    "Director must start the reconciliation worker",
)

require(
    "director_storage_projection_reconcile_worker.stop_and_join();" in main,
    "Director shutdown must join the reconciliation worker",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection reconciliation worker structural guards passed")
