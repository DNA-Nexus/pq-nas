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

start_marker = '    plan["operation"]       = "destroy-pool";\n'
end_marker = '    // Enqueue (fail-closed)\n'

start = text.find(start_marker)
end = text.find(end_marker, start if start >= 0 else 0)

if start < 0:
    fail("missing destroy-pool section start")
    section = ""
elif end < 0:
    fail("missing destroy-pool section end")
    section = ""
else:
    section = text[start:end]

if section:
    required = [
        "destroy-pool root-helper steps route",
        "through argv, not shell command strings",
        'commands.push_back("RAID_ROOT udev-settle");',
        'commands.push_back("RAID_ROOT umount-pool " + resolved_mount);',
        'commands.push_back("RAID_ROOT btrfs-device-scan");',
        'commands.push_back("RAID_ROOT wipefs " + dev);',
        'commands.push_back("RAID_ROOT zap-disk " + dev);',
        'commands.push_back(std::string("POOLS_CFG_REMOVE ") + resolved_mount);',
        'commands.push_back(std::string("FSTAB_REMOVE ") + resolved_mount);',
        'commands.push_back("RAID_ROOT rmdir-pool " + resolved_mount);',
    ]

    for needle in required:
        if needle not in section:
            fail(f"missing destroy-pool RAID_ROOT marker: {needle}")

    udev_count = section.count('commands.push_back("RAID_ROOT udev-settle");')
    if udev_count != 2:
        fail(f"expected 2 destroy-pool RAID_ROOT udev-settle entries, found {udev_count}")

    legacy = [
        'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root udev-settle");',
        'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root umount-pool " + sh_quote(resolved_mount));',
        'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-device-scan");',
        'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root wipefs " + sh_quote(dev));',
        'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root zap-disk " + sh_quote(dev));',
        'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root rmdir-pool " + sh_quote(resolved_mount));',
    ]

    for bad in legacy:
        if bad in section:
            fail(f"legacy destroy-pool pqnas-raid-root shell string still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: destroy-pool root-helper command-list entries use RAID_ROOT pseudo-commands.")
print("OK: destroy-pool scoped legacy pqnas-raid-root shell strings are gone.")
PY
