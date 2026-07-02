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

required_global = [
    "Pseudo-command: RAID_ROOT <action> [args...]",
    "raid_root_args_are_supported(args)",
    '"/usr/local/sbin/pqnas-raid-root"',
]

for needle in required_global:
    if needle not in text:
        fail(f"missing RAID_ROOT handler marker: {needle}")

scrub_start_count = text.count('commands.push_back("RAID_ROOT btrfs-scrub-start " + resolved_mount);')
if scrub_start_count != 2:
    fail(f"expected 2 RAID_ROOT scrub-start command-list entries, found {scrub_start_count}")

scrub_status_count = text.count('commands.push_back("BTRFS_STATUS scrub-status " + resolved_mount);')
if scrub_status_count != 2:
    fail(f"expected 2 BTRFS_STATUS scrub-status entries next to scrub plans, found {scrub_status_count}")

comment_count = text.count("root-helper scrub start through argv, not a shell command string")
if comment_count != 2:
    fail(f"expected 2 scrub-start security comments, found {comment_count}")

legacy = 'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-scrub-start " + sh_quote(resolved_mount));'
if legacy in text:
    fail("legacy scrub-start pqnas-raid-root shell-string command-list entry still exists")

if failed:
    sys.exit(1)

print("OK: scrub-start command-list entries use RAID_ROOT pseudo-commands.")
print("OK: legacy scrub-start pqnas-raid-root shell strings are gone.")
PY
