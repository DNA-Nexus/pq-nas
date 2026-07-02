#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

main = Path("server/src/main.cpp")
text = main.read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

removed_symbols = [
    "btrfs_filesystem_has_device",
    "lsblk_disk_mountpoints_json",
    "storage_list_disks_json",
    "detect_system_pool_root_disk",
    "storage_btrfs_status_json",
    "validate_create_pool_devices",
    "build_create_pool_commands_json",
    "compute_create_pool_plan_id",
    "sh_quote",
    "getenv_bool",
    "is_dev_path_basic_safe",
    "shell_escape_single_quotes",
]

for sym in removed_symbols:
    if sym in text:
        fail(f"dead main.cpp storage/RAID symbol remains: {sym}")

legacy_markers = [
    'static int run_capture(const std::string& cmd, std::string* out)',
    'run_capture("/usr/bin/lsblk -J -b -O " + sh_quote(disk_path)',
    'run_capture("lsblk -J -b -O 2>/dev/null"',
    'run_cmd_capture("/usr/bin/findmnt -no SOURCE --target " + sh_quote(root)',
    'run_capture(cmd_show,  &show)',
    'run_capture(cmd_df,    &df)',
    'run_capture(cmd_stats, &stats)',
    'cmds.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root',
    'std::string mkfs = "/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root',
    'cmds.push_back("/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show',
]

for marker in legacy_markers:
    if marker in text:
        fail(f"legacy main.cpp storage shell marker remains: {marker}")

required_active_markers = [
    "main_run_argv_capture_no_shell",
    "pool_mounts_restore_managed(users_path)",
    "register_storage_raid_routes(srv, StorageRaidRoutesContext",
]

for marker in required_active_markers:
    if marker not in text:
        fail(f"active split/startup marker unexpectedly missing: {marker}")

if failed:
    sys.exit(1)

print("OK: dead main.cpp storage/RAID shell helper copies are removed.")
print("OK: active split route and startup argv restore markers remain.")
PY
