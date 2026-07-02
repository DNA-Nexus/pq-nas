#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

route = Path("server/src/routes/routes_storage_raid.cpp")
if not route.is_file():
    print(f"ERROR: missing route file: {route}")
    sys.exit(1)

text = route.read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

start_marker = "    // Helper to run btrfs-status actions via argv + capture"
end_marker = "    // Busy (mount lock exists)"

start = text.find(start_marker)
end = text.find(end_marker, start if start >= 0 else 0)

if start < 0:
    fail("missing RAID status argv helper section start")
    section = ""
elif end < 0:
    fail("missing RAID status argv helper section end")
    section = ""
else:
    section = text[start:end]

if section:
    required = [
        "auto run_btrfs_status = [&](const std::string& action",
        "run_btrfs_status_helper_argv(action, resolved_mount, &out, &rc)",
        "resolved mount targets cannot be shell-interpreted",
        'run_btrfs_status(\n        "filesystem-show"',
        'run_btrfs_status(\n        "filesystem-df"',
        'run_btrfs_status(\n        "device-stats"',
        'run_btrfs_status(\n            "balance-status"',
        'run_btrfs_status(\n            "scrub-status"',
    ]

    for needle in required:
        if needle not in section:
            fail(f"RAID status section missing argv marker: {needle}")

    for bad in [
        "sudo -n /usr/local/sbin/pqnas-btrfs-status",
        "/usr/local/sbin/pqnas-btrfs-status filesystem-show",
        "/usr/local/sbin/pqnas-btrfs-status filesystem-df",
        "/usr/local/sbin/pqnas-btrfs-status device-stats",
        "/usr/local/sbin/pqnas-btrfs-status balance-status",
        "/usr/local/sbin/pqnas-btrfs-status scrub-status",
        "auto run = [&](const std::string& cmd",
        "run_cmd_capture(cmd, &out, &rc)",
        "sh_quote(resolved_mount)",
    ]:
        if bad in section:
            fail(f"RAID status section still contains legacy shell-string marker: {bad}")

if failed:
    sys.exit(1)

print("OK: RAID status endpoint btrfs-status probes use direct argv helper calls.")
print("OK: RAID status endpoint no longer builds pqnas-btrfs-status shell strings.")
PY
