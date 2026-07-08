#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

route = Path("server/src/routes/routes_storage_raid.cpp")
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
        fail(f"missing opening brace for: {signature}")
        return ""

    depth = 0
    i = brace
    while i < len(text):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return text[brace + 1:i]
        i += 1

    fail(f"missing closing brace for: {signature}")
    return ""

for bad in [
    "popen(",
    "pclose(",
    "cmd2.c_str()",
    'cmd2 += " 2>&1"',
]:
    if bad in text:
        fail(f"legacy shell fallback marker remains: {bad}")

for needle in [
    "raid_try_run_known_root_helper_argv(cmd, out, &helper_ec)",
    "raid_try_run_known_root_helper_argv(cmd, out, exit_code)",
    "raid_try_run_nonroot_probe_argv(cmd, out, &probe_ec)",
    "RAID_ROOT",
    "BTRFS_STATUS",
    "FSTAB_ADD_BTRFS",
    "FSTAB_REMOVE",
    "unsupported RAID command",
    "unsupported RAID capture command",
]:
    if needle not in text:
        fail(f"missing argv/fail-closed marker: {needle}")

root_helper = function_body("static bool raid_try_run_known_root_helper_argv(const std::string& cmd_in,")
if root_helper:
    for marker in [
        'cmd.rfind("RAID_ROOT ", 0)',
        "raid_root_args_are_supported(args)",
        '"/usr/local/sbin/pqnas-raid-root"',
        "argv.insert(argv.end(), args.begin(), args.end())",
        "unsupported RAID_ROOT command",
        'cmd.rfind("BTRFS_STATUS ", 0)',
        "raid_btrfs_status_args_are_supported(args)",
        '"/usr/local/sbin/pqnas-btrfs-status"',
        "unsupported BTRFS_STATUS command",
    ]:
        if marker not in root_helper:
            fail(f"raid root helper dispatcher missing pseudo-command marker: {marker}")

run_capture = function_body("[[maybe_unused]] static int run_capture(const std::string& cmd, std::string* out)")
if run_capture:
    if "raid_try_run_known_root_helper_argv" not in run_capture:
        fail("run_capture must route known root helpers before failing closed")
    if "raid_try_run_nonroot_probe_argv" not in run_capture:
        fail("run_capture must route known non-root probes before failing closed")
    if "unsupported RAID capture command" not in run_capture:
        fail("run_capture must fail closed for unsupported command strings")

run_cmd = function_body("static bool run_cmd_capture(const std::string& cmd, std::string* out, int* exit_code)")
if run_cmd:
    for marker in [
        "run_fstab_pseudo_argv",
        "raid_try_run_known_root_helper_argv",
        "raid_try_run_nonroot_probe_argv",
        "unsupported RAID command",
    ]:
        if marker not in run_cmd:
            fail(f"run_cmd_capture missing expected marker: {marker}")

if failed:
    sys.exit(1)

print("OK: RAID plan executors route supported commands through argv handlers.")
print("OK: unsupported RAID command strings fail closed without shell fallback.")
PY
