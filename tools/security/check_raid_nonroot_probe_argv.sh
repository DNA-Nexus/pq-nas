#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

route = Path("server/src/routes/routes_storage_raid.cpp")
text = route.read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required = [
    "raid_try_run_nonroot_probe_argv",
    "raid_strip_probe_redirect_suffix",
    "raid_probe_abs_path_arg_is_safe",
    '"/usr/bin/lsblk"',
    '"/usr/bin/findmnt"',
    'run_lsblk_json_all_props_argv',
    'run_findmnt_no_target_argv',
    'run_findmnt_btrfs_list_argv',
    "unsupported RAID capture command",
    "unsupported RAID command",
]

for needle in required:
    if needle not in text:
        fail(f"missing non-root probe argv/fail-closed marker: {needle}")

for bad in [
    "popen(",
    "pclose(",
    '"/usr/bin/findmnt -no ',
    '"/usr/bin/findmnt -rn -t btrfs -o ',
    'run_capture("lsblk -J -b -O 2>/dev/null"',
]:
    if bad in text:
        fail(f"legacy non-root probe shell marker remains: {bad}")

if failed:
    sys.exit(1)

print("OK: RAID non-root lsblk/findmnt probes route through argv handlers.")
print("OK: unsupported command strings fail closed without shell fallback.")
PY
