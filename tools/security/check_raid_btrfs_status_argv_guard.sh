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
    "raid_btrfs_status_args_are_supported",
    'args.size() != 2',
    'action == "filesystem-show"',
    'action == "filesystem-df"',
    'action == "filesystem-df-bytes"',
    'action == "filesystem-usage"',
    'action == "filesystem-usage-bytes"',
    'action == "device-stats"',
    'action == "scrub-status"',
    'action == "balance-status"',
    'mount.find("..")',
    '"/usr/local/sbin/pqnas-btrfs-status"',
    '"err: unsupported btrfs status helper command\\n"',
]

for needle in required:
    if needle not in text:
        fail(f"missing required btrfs-status argv guard marker: {needle}")

body = function_body("static bool raid_try_run_known_root_helper_argv(")
if body:
    validate_pos = body.find("raid_btrfs_status_args_are_supported(args)")
    argv_pos = body.find("std::vector<std::string> argv = {")
    run_pos = body.find("return run_argv_capture_limited(")
    popen_pos = body.find("popen(")

    if validate_pos < 0:
        fail("root-helper interceptor does not validate btrfs-status args")
    if argv_pos < 0:
        fail("root-helper interceptor missing argv construction")
    if run_pos < 0:
        fail("root-helper interceptor missing argv execution")
    if validate_pos >= 0 and argv_pos >= 0 and validate_pos > argv_pos:
        fail("btrfs-status validation happens after argv construction")
    if validate_pos >= 0 and run_pos >= 0 and validate_pos > run_pos:
        fail("btrfs-status validation happens after argv execution")
    if popen_pos >= 0:
        fail("root-helper interceptor must not contain popen fallback")

allow_body = function_body("static bool raid_btrfs_status_args_are_supported(")
if allow_body:
    for bad in ["popen(", "system(", "/bin/sh", "sudo -n"]:
        if bad in allow_body:
            fail(f"btrfs-status args guard must not use shell primitive: {bad}")

if failed:
    sys.exit(1)

print("OK: RAID btrfs-status argv interceptor validates action and mount before sudo argv execution.")
print("OK: btrfs-status validation remains inside the root-helper interception path, before fallback.")
PY
