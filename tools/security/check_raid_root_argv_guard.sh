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

def function_body(signature: str) -> str:
    start = text.find(signature)
    if start < 0:
        fail(f"missing function: {signature}")
        return ""

    brace = text.find("{", start)
    if brace < 0:
        fail(f"missing function body for: {signature}")
        return ""

    depth = 0
    for i in range(brace, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace:i + 1]

    fail(f"unterminated function body for: {signature}")
    return ""

required = [
    "raid_root_args_are_supported",
    "raid_root_dev_arg_is_supported",
    "raid_root_pool_arg_is_supported",
    "raid_root_pool_data_arg_is_supported",
    "raid_root_label_arg_is_supported",
    "raid_root_mount_spec_arg_is_supported",
    '"/usr/local/sbin/pqnas-raid-root"',
    '"err: unsupported raid root helper command\\n"',
    'action == "zap-disk"',
    'action == "create-btrfs-partition"',
    'action == "partprobe"',
    'action == "wipefs"',
    'action == "mkfs-btrfs"',
    'action == "mkdir-p"',
    'action == "chown-pqnas"',
    'action == "chmod-0755"',
    'action == "mount-label"',
    'action == "mount-spec"',
    'action == "umount-pool"',
    'action == "rmdir-pool"',
    'action == "udev-settle"',
    'action == "btrfs-device-scan"',
    'action == "btrfs-scrub-start"',
    'action == "btrfs-device-add"',
    'action == "btrfs-device-remove"',
    'action == "btrfs-balance-raid1"',
    'action == "btrfs-balance-single-force"',
    'action == "btrfs-balance-force-profile"',
]

for needle in required:
    if needle not in text:
        fail(f"missing required RAID root argv guard marker: {needle}")

body = function_body("static bool raid_try_run_known_root_helper_argv(")
if body:
    root_validate_pos = body.find("raid_root_args_are_supported(args)")
    btrfs_validate_pos = body.find("raid_btrfs_status_args_are_supported(args)")
    argv_pos = body.find("std::vector<std::string> argv = {")
    run_pos = body.find("return run_argv_capture_limited(")
    popen_pos = body.find("popen(")

    if root_validate_pos < 0:
        fail("root-helper interceptor does not validate pqnas-raid-root args")
    if btrfs_validate_pos < 0:
        fail("root-helper interceptor lost btrfs-status validation")
    if argv_pos < 0:
        fail("root-helper interceptor missing argv construction")
    if run_pos < 0:
        fail("root-helper interceptor missing argv execution")
    if root_validate_pos >= 0 and argv_pos >= 0 and root_validate_pos > argv_pos:
        fail("pqnas-raid-root validation happens after argv construction")
    if root_validate_pos >= 0 and run_pos >= 0 and root_validate_pos > run_pos:
        fail("pqnas-raid-root validation happens after argv execution")
    if popen_pos >= 0:
        fail("root-helper interceptor must not contain popen fallback")

allow_body = function_body("static bool raid_root_args_are_supported(")
if allow_body:
    for bad in ["popen(", "system(", "/bin/sh", "sudo -n"]:
        if bad in allow_body:
            fail(f"pqnas-raid-root args guard must not use shell primitive: {bad}")

if failed:
    sys.exit(1)

print("OK: RAID root-helper argv interceptor validates action and arguments before sudo argv execution.")
print("OK: RAID root-helper validation remains inside the root-helper interception path, before fallback.")
PY
