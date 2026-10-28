#!/usr/bin/env python3

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MAIN = ROOT / "server/src/main.cpp"

source = MAIN.read_text(encoding="utf-8")
failures = []

main_match = re.search(r"\bint\s+main\s*\(", source)
listen_marker = "srv.listen(listen_host, LISTEN_PORT);"

snapshot_decl = "std::thread snapshots_thread;"
snapshot_start = "pqnas::snapshots::start_snapshot_scheduler("
drive_start = "pqnas::drive_health_monitor_start("

if not main_match:
    failures.append("main() anchor missing")
    main_pos = -1
else:
    main_pos = main_match.start()

listen_pos = (
    source.find(listen_marker, main_pos)
    if main_pos >= 0
    else -1
)

if listen_pos < 0:
    failures.append("server listen anchor missing")

for marker, name in (
    (snapshot_decl, "snapshot thread declaration"),
    (snapshot_start, "snapshot scheduler start"),
    (drive_start, "drive-health monitor start"),
):
    count = source.count(marker)

    if count != 1:
        failures.append(
            f"{name}: expected exactly one occurrence, found {count}"
        )

if main_pos >= 0 and listen_pos >= 0:
    snapshot_start_pos = source.find(
        snapshot_start,
        main_pos,
        listen_pos
    )

    drive_start_pos = source.find(
        drive_start,
        main_pos,
        listen_pos
    )

    if snapshot_start_pos < 0:
        failures.append(
            "snapshot scheduler must start before server listen"
        )

    if drive_start_pos < 0:
        failures.append(
            "drive-health monitor must start before server listen"
        )

    if snapshot_start_pos >= 0:
        snapshot_decl_pos = source.find(
            snapshot_decl,
            main_pos,
            snapshot_start_pos
        )

        if snapshot_decl_pos < 0:
            failures.append(
                "snapshot thread must be declared non-joinable "
                "before scheduler startup"
            )

    starts = [
        pos
        for pos in (
            snapshot_start_pos,
            drive_start_pos,
        )
        if pos >= 0
    ]

    if len(starts) == 2:
        first_non_raii_start = min(starts)

        dangerous_window = source[
            first_non_raii_start:listen_pos
        ]

        if re.search(
            r"\breturn\s+[1-9][0-9]*\s*;",
            dangerous_window
        ):
            failures.append(
                "non-zero return remains after non-RAII "
                "background workers become joinable"
            )

snapshot_stop = (
    "if (snapshots_thread.joinable()) "
    "snapshots_thread.join();"
)

drive_stop = "pqnas::drive_health_monitor_stop();"

for marker, name in (
    (snapshot_stop, "snapshot shutdown join"),
    (drive_stop, "drive-health shutdown stop"),
):
    pos = source.find(marker)

    if pos < 0:
        failures.append(f"{name} missing")
    elif listen_pos >= 0 and pos <= listen_pos:
        failures.append(
            f"{name} must remain after server listen lifetime"
        )

if failures:
    print("FAIL: startup background thread lifetime guard")

    for failure in failures:
        print(f"  - {failure}")

    raise SystemExit(1)

print(
    "PASS: non-RAII background workers start only "
    "after fatal startup returns"
)
