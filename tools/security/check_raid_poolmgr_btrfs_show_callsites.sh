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

plan = section_between(
    "// ----- POST /api/v4/poolmgr/plan-layout",
    "// ----- POST /api/v4/poolmgr/apply-layout",
    "poolmgr plan-layout"
)

apply = section_between(
    "// ----- POST /api/v4/poolmgr/apply-layout",
    "// ----- GET /api/v4/raid/discovery",
    "poolmgr apply-layout"
)

for label, section in [
    ("poolmgr plan-layout", plan),
    ("poolmgr apply-layout", apply),
]:
    if not section:
        continue

    required = 'run_btrfs_status_helper_capture("filesystem-show", mount, &show_out)'
    if required not in section:
        fail(f"{label} missing direct argv helper call: {required}")

    if "pool manager mounts cannot be shell-interpreted" not in section:
        fail(f"{label} missing security comment for argv helper conversion")

    for bad in [
        "sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show",
        "/usr/local/sbin/pqnas-btrfs-status filesystem-show",
        "run_capture(\"/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show",
    ]:
        if bad in section:
            fail(f"{label} still contains legacy shell-string marker: {bad}")

if failed:
    sys.exit(1)

print("OK: poolmgr plan/apply filesystem-show probes use direct btrfs-status argv helper calls.")
print("OK: poolmgr plan/apply sections no longer build pqnas-btrfs-status filesystem-show shell strings.")
PY
