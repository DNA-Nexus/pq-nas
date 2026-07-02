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
    "Pseudo-command: BTRFS_STATUS <action> <mount>",
    "plan mounts cannot be shell-interpreted",
    "run_btrfs_status_helper_capture(action, mount, &status_out)",
    'cmds.push_back("BTRFS_STATUS filesystem-show " + mount)',
    'run_btrfs_status_helper_capture("filesystem-show", mount, &show_out)',
    "newly-created pool mounts cannot be shell-interpreted",
]

for needle in required:
    if needle not in text:
        fail(f"missing final btrfs-status argv marker: {needle}")

scrub_count = text.count('commands.push_back("BTRFS_STATUS scrub-status " + resolved_mount);')
if scrub_count != 2:
    fail(f"expected 2 BTRFS_STATUS scrub-status command-list entries, found {scrub_count}")

legacy = [
    'cmds.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show " + sh_quote(mount));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status scrub-status " + sh_quote(resolved_mount));',
    '"/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show " + sh_quote(mount) + " 2>&1"',
]

for bad in legacy:
    if bad in text:
        fail(f"legacy final btrfs-status shell-string call site still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: final btrfs-status command-list call sites use argv-backed pseudo-commands.")
print("OK: final direct filesystem-show post-exec call uses argv helper.")
PY
