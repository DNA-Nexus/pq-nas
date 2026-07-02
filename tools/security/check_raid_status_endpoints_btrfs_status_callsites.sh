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

def section_between(start_marker: str, end_marker: str, label: str) -> str:
    start = text.find(start_marker)
    end = text.find(end_marker, start if start >= 0 else 0)

    if start < 0:
        fail(f"missing {label} section start")
        return ""
    if end < 0:
        fail(f"missing {label} section end")
        return ""

    return text[start:end]

balance = section_between(
    "// ----- GET /api/v4/raid/balance-status?mount=/path",
    "// ----- GET /api/v4/raid/scrub-status?mount=/path",
    "balance-status"
)

scrub = section_between(
    "// ----- GET /api/v4/raid/scrub-status?mount=/path",
    "// ----- POST /api/v4/raid/plan/scrub",
    "scrub-status"
)

checks = [
    (
        "balance-status",
        balance,
        'run_btrfs_status_helper_argv("balance-status", resolved_mount, &out, &rc)'
    ),
    (
        "scrub-status",
        scrub,
        'run_btrfs_status_helper_argv("scrub-status", resolved_mount, &out, &rc)'
    ),
]

for label, section, required in checks:
    if not section:
        continue

    if required not in section:
        fail(f"{label} section missing direct argv helper call: {required}")

    if "resolved mount targets cannot be shell-interpreted" not in section:
        fail(f"{label} section missing security comment for argv helper conversion")

    for bad in [
        "sudo -n /usr/local/sbin/pqnas-btrfs-status",
        "/usr/local/sbin/pqnas-btrfs-status balance-status",
        "/usr/local/sbin/pqnas-btrfs-status scrub-status",
        "sh_quote(resolved_mount)",
        "const std::string cmd =",
        "run_cmd_capture(cmd, &out, &rc)",
    ]:
        if bad in section:
            fail(f"{label} section still contains legacy shell-string marker: {bad}")

if failed:
    sys.exit(1)

print("OK: RAID balance/scrub status endpoints use direct btrfs-status argv helper calls.")
print("OK: RAID status endpoints no longer build pqnas-btrfs-status shell strings.")
PY
