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
    "Pseudo-command: RAID_ROOT <action> [args...]",
    "raid_root_args_are_supported(args)",
    '"/usr/local/sbin/pqnas-raid-root"',
    'commands.push_back("RAID_ROOT btrfs-scrub-start " + resolved_mount);',
    'commands.push_back("RAID_ROOT btrfs-device-add " + new_part + " " + resolved_mount);',
    'commands.push_back("RAID_ROOT btrfs-device-remove " + member_path + " " + resolved_mount);',
    'commands.push_back("RAID_ROOT umount-pool " + resolved_mount);',
    'commands.push_back("RAID_ROOT rmdir-pool " + resolved_mount);',
    'std::string mkfs = "RAID_ROOT mkfs-btrfs " + mode + " " + label;',
]

for needle in required:
    if needle not in text:
        fail(f"missing final RAID_ROOT migration marker: {needle}")

legacy_patterns = [
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root',
    'cmds.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root',
    'std::string mkfs = "/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root',
    'commands.push_back(\n            "/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root',
    'cmds.push_back(\n            "/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root',
]

for bad in legacy_patterns:
    if bad in text:
        fail(f"legacy pqnas-raid-root command-list shell string still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: pqnas-raid-root command-list call sites use RAID_ROOT pseudo-commands.")
print("OK: no raw pqnas-raid-root shell-string command-list builders remain.")
PY
