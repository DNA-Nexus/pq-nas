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

for bad in [
    "popen(",
    "pclose(",
    "cmd2.c_str()",
    'cmd2 += " 2>&1"',
    "FILE* pipe",
    "FILE* fp",
]:
    if bad in text:
        fail(f"legacy shell fallback marker remains: {bad}")

required = [
    "raid_try_run_known_root_helper_argv(cmd, out, &helper_ec)",
    "raid_try_run_nonroot_probe_argv(cmd, out, &probe_ec)",
    "raid_try_run_known_root_helper_argv(cmd, out, exit_code)",
    "raid_try_run_nonroot_probe_argv(cmd, out, &probe_ec)",
    "unsupported RAID capture command",
    "unsupported RAID command",
    "fail closed instead of reaching a shell",
]

for needle in required:
    if needle not in text:
        fail(f"missing fail-closed marker: {needle}")

if failed:
    sys.exit(1)

print("OK: RAID capture helpers have no shell fallback.")
print("OK: unsupported RAID command strings fail closed after argv dispatchers.")
PY
