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

for needle in [
    "Pseudo-command: RAID_ROOT <action> [args...]",
    "raid_root_args_are_supported(args)",
    '"/usr/local/sbin/pqnas-raid-root"',
]:
    if needle not in text:
        fail(f"missing RAID_ROOT handler marker: {needle}")

required_counts = {
    "profile-conversion root-helper steps route": 2,
    "device-remove root-helper steps route": 2,
    'commands.push_back("RAID_ROOT btrfs-balance-raid1 " + resolved_mount);': 4,
    'commands.push_back("RAID_ROOT btrfs-balance-single-force " + resolved_mount);': 4,
    'commands.push_back("RAID_ROOT btrfs-device-remove " + member_path + " " + resolved_mount);': 2,
}

for needle, expected in required_counts.items():
    got = text.count(needle)
    if got != expected:
        fail(f"expected {expected} marker(s), found {got}: {needle}")

legacy = [
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-balance-raid1 " + sh_quote(resolved_mount));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-balance-single-force " + sh_quote(resolved_mount));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-device-remove " + sh_quote(member_path) + " " + sh_quote(resolved_mount));',
    '"/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-balance-single-force "\n            + sh_quote(resolved_mount)',
]

for bad in legacy:
    if bad in text:
        fail(f"legacy balance/remove pqnas-raid-root shell string still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: balance/profile/remove command-list entries use RAID_ROOT pseudo-commands.")
print("OK: legacy balance/remove pqnas-raid-root shell strings are gone.")
PY
