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

required = [
    'expected UUID mounts cannot be shell-interpreted',
    'run_btrfs_status_helper_capture("filesystem-show", mount, &show_out)',
    'run_btrfs_status_helper_argv("scrub-status", resolved_mount, &s_out, &s_rc)',
    'run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show)',
]

for needle in required:
    if needle not in text:
        fail(f"missing argv helper marker: {needle}")

legacy = [
    'run_capture("/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show " + sh_quote(mount) + " 2>&1", &show_out)',
    '(void)run_cmd_capture("/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status scrub-status " + sh_quote(resolved_mount), &s_out, &s_rc)',
    '"/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show " + sh_quote(resolved_mount) + " 2>&1"',
]

for bad in legacy:
    if bad in text:
        fail(f"legacy direct read-only btrfs-status shell string still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: direct read-only btrfs-status tail call sites use argv helpers.")
print("OK: expect_uuid, scrub snapshot, and membership-read shell strings are gone.")
PY
