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

start_marker = "    // -------------------- btrfs read-only health commands --------------------"
end_marker = "    // Parsed scrub summary"

start = text.find(start_marker)
end = text.find(end_marker, start if start >= 0 else 0)

if start < 0:
    fail("missing RAID detail status health command section start")
    section = ""
elif end < 0:
    fail("missing RAID detail status health command section end")
    section = ""
else:
    section = text[start:end]

if section:
    required = [
        'resolved mount targets cannot be shell-interpreted',
        'run_btrfs_status_helper_capture("device-stats", resolved_mount, &dev_stats)',
        'run_btrfs_status_helper_capture("scrub-status", resolved_mount, &scrub_status)',
        'run_btrfs_status_helper_capture("balance-status", resolved_mount, &balance_status)',
    ]

    for needle in required:
        if needle not in section:
            fail(f"missing RAID detail status argv marker: {needle}")

    legacy = [
        "const std::string mp = sh_quote(resolved_mount)",
        '"/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status device-stats " + mp',
        '"/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status scrub-status " + mp',
        '"/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status balance-status " + mp',
        "run_capture(cmd_dev_stats, &dev_stats)",
        "run_capture(cmd_scrub,     &scrub_status)",
        "run_capture(cmd_balance,   &balance_status)",
    ]

    for bad in legacy:
        if bad in section:
            fail(f"RAID detail status still contains legacy shell-string marker: {bad}")

if failed:
    sys.exit(1)

print("OK: RAID detail status btrfs health commands use argv helper calls.")
print("OK: RAID detail status no longer builds btrfs-status shell strings.")
PY
