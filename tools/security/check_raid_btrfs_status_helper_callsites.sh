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
    "run_btrfs_status_helper_argv",
    "run_btrfs_status_helper_capture",
    "raid_btrfs_status_args_are_supported(args)",
    '"/usr/bin/sudo"',
    '"-n"',
    '"/usr/local/sbin/pqnas-btrfs-status"',
    '"filesystem-show"',
    '"filesystem-df"',
    '"device-stats"',
]

for needle in required:
    if needle not in text:
        fail(f"missing required btrfs-status helper callsite marker: {needle}")

helper_body = function_body("static bool run_btrfs_status_helper_argv(")
if helper_body:
    validate_pos = helper_body.find("raid_btrfs_status_args_are_supported(args)")
    run_pos = helper_body.find("run_argv_capture_limited")
    if validate_pos < 0:
        fail("direct btrfs-status helper does not validate action/mount")
    if run_pos < 0:
        fail("direct btrfs-status helper does not use argv execution")
    if validate_pos >= 0 and run_pos >= 0 and validate_pos > run_pos:
        fail("direct btrfs-status helper validates after argv execution")

    for bad in ["popen(", "system(", "/bin/sh", " sh_quote("]:
        if bad in helper_body:
            fail(f"direct btrfs-status helper must not use shell primitive: {bad}")

storage_body = function_body("static json storage_btrfs_status_json(")
if storage_body:
    for marker in [
        'run_btrfs_status_helper_capture("filesystem-show"',
        'run_btrfs_status_helper_capture("filesystem-df"',
        'run_btrfs_status_helper_capture("device-stats"',
    ]:
        if marker not in storage_body:
            fail(f"storage_btrfs_status_json missing direct helper call: {marker}")

    for bad in [
        "/usr/local/sbin/pqnas-btrfs-status",
        "sudo -n",
        "sh_quote(mountpoint)",
        "cmd_show",
        "cmd_df",
        "cmd_stats",
    ]:
        if bad in storage_body:
            fail(f"storage_btrfs_status_json still contains legacy shell-string marker: {bad}")

hasdev_body = function_body("static bool btrfs_filesystem_has_device(")
if hasdev_body:
    if 'run_btrfs_status_helper_argv("filesystem-show", mount, &show, &ec)' not in hasdev_body:
        fail("btrfs_filesystem_has_device missing direct filesystem-show helper call")

    for bad in [
        "/usr/local/sbin/pqnas-btrfs-status",
        "sudo -n",
        "sh_quote(mount)",
        "const std::string cmd",
    ]:
        if bad in hasdev_body:
            fail(f"btrfs_filesystem_has_device still contains legacy shell-string marker: {bad}")

if failed:
    sys.exit(1)

print("OK: first RAID btrfs-status call sites use direct argv helper calls.")
print("OK: converted call sites no longer build pqnas-btrfs-status shell strings.")
PY
