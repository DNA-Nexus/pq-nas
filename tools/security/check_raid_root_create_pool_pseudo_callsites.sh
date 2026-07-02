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

# Pseudo-command handler must exist and must validate before argv execution.
handler_required = [
    "Pseudo-command: RAID_ROOT <action> [args...]",
    "create-pool plan root-helper steps route through argv",
    "raid_root_args_are_supported(args)",
    '"/usr/local/sbin/pqnas-raid-root"',
    "argv.insert(argv.end(), args.begin(), args.end())",
]

for needle in handler_required:
    if needle not in text:
        fail(f"missing RAID_ROOT pseudo-command handler marker: {needle}")

start_marker = '    const std::string mount = root + "/pools/" + pool_id;'
end_marker = '    return cmds;\n}\n\n\n// copied transitional helper from main.cpp: compute_create_pool_plan_id'

start = text.find(start_marker)
end = text.find(end_marker, start if start >= 0 else 0)

if start < 0:
    fail("missing create_pool_plan_cmds section start")
    section = ""
elif end < 0:
    fail("missing create_pool_plan_cmds section end")
    section = ""
else:
    section = text[start:end]

if section:
    required = [
        "root-helper steps through argv, not shell command strings",
        'cmds.push_back("RAID_ROOT zap-disk " + d)',
        'cmds.push_back("RAID_ROOT partprobe " + d)',
        'cmds.push_back("RAID_ROOT wipefs " + d)',
        'std::string mkfs = "RAID_ROOT mkfs-btrfs " + mode + " " + label',
        'for (const auto& d : devices) mkfs += " " + d',
        'cmds.push_back("RAID_ROOT mkdir-p " + mount)',
        'cmds.push_back("RAID_ROOT mount-label " + label + " " + mount)',
        'cmds.push_back("RAID_ROOT udev-settle")',
        'cmds.push_back("RAID_ROOT btrfs-device-scan")',
        'cmds.push_back("RAID_ROOT chown-pqnas " + data_dir)',
        'cmds.push_back("RAID_ROOT chmod-0755 " + data_dir)',
    ]

    for needle in required:
        if needle not in section:
            fail(f"missing create-pool RAID_ROOT marker: {needle}")

    mkdir_count = section.count('cmds.push_back("RAID_ROOT mkdir-p " +')
    if mkdir_count != 2:
        fail(f"expected 2 create-pool RAID_ROOT mkdir-p entries, found {mkdir_count}")

    if "/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root" in section:
        fail("create_pool_plan_cmds still builds pqnas-raid-root shell command strings")

if failed:
    sys.exit(1)

print("OK: create-pool root-helper command-list entries use RAID_ROOT pseudo-commands.")
print("OK: RAID_ROOT pseudo-command validates args and executes pqnas-raid-root via argv.")
PY
