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

def section_between(name: str, start_marker: str, end_marker: str) -> str:
    start = text.find(start_marker)
    if start < 0:
        fail(f"missing {name} start marker")
        return ""
    end = text.find(end_marker, start)
    if end < 0:
        fail(f"missing {name} end marker")
        return ""
    return text[start:end]

# Security: pick only the add-device plan block. Other endpoints also contain
# generic "PLAN ONLY" text, so anchor on the add-device new_disk sanity check.
plan_section = section_between(
    "add-device plan section",
    '    steps.push_back("Sanity-check: new_disk is allowlisted by lsblk and has no mounted partitions.");\n',
    '    plan["steps"] = steps;\n'
)

# There are several "Build commands exactly like plan endpoint" blocks.
# Security: pick only the add-device execute block by requiring new_part
# btrfs-device-add before the plan_id check.
execute_section = ""
search_from = 0
while True:
    start = text.find('    // -------- Build commands exactly like plan endpoint --------\n', search_from)
    if start < 0:
        break

    end = text.find('    // plan_id check (must match exactly)\n', start)
    if end < 0:
        search_from = start + 1
        continue

    candidate = text[start:end]
    if 'commands.push_back("RAID_ROOT btrfs-device-add " + new_part + " " + resolved_mount);' in candidate:
        execute_section = candidate
        break

    search_from = start + 1

if not execute_section:
    fail("missing add-device execute section with RAID_ROOT btrfs-device-add new_part marker")

required_each_section = [
    "route through argv, not shell command strings",
    'commands.push_back("RAID_ROOT zap-disk " + new_disk);',
    'commands.push_back("RAID_ROOT wipefs " + new_disk);',
    'commands.push_back("RAID_ROOT create-btrfs-partition " + new_disk);',
    'commands.push_back("RAID_ROOT partprobe " + new_disk);',
    'commands.push_back("RAID_ROOT udev-settle");',
    'commands.push_back("WAIT_BLOCK " + new_part + " 2000");',
    'commands.push_back("RAID_ROOT wipefs " + new_part);',
    'commands.push_back("RAID_ROOT btrfs-device-add " + new_part + " " + resolved_mount);',
    'commands.push_back("RAID_ROOT btrfs-balance-raid1 " + resolved_mount);',
    'commands.push_back(std::string("POOLS_CFG_SET_MODE ") + resolved_mount + " raid1");',
]

legacy_each_section = [
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root zap-disk " + sh_quote(new_disk));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root wipefs " + sh_quote(new_disk));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root create-btrfs-partition " + sh_quote(new_disk));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root partprobe " + sh_quote(new_disk));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root udev-settle");',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root wipefs " + sh_quote(new_part));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-device-add " + sh_quote(new_part) + " " + sh_quote(resolved_mount));',
    'commands.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-balance-raid1 " + sh_quote(resolved_mount));',
]

for name, section in [
    ("add-device plan section", plan_section),
    ("add-device execute section", execute_section),
]:
    if not section:
        continue

    for needle in required_each_section:
        if needle not in section:
            fail(f"missing {name} RAID_ROOT marker: {needle}")

    for bad in legacy_each_section:
        if bad in section:
            fail(f"legacy {name} pqnas-raid-root shell string still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: add-device plan/execute root-helper command-list entries use RAID_ROOT pseudo-commands.")
print("OK: add-device scoped legacy pqnas-raid-root shell strings are gone.")
PY
