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
    "raid_try_run_nonroot_probe_argv",
    "raid_probe_abs_path_arg_is_safe",
    "raid_strip_probe_redirect_suffix",
    '"/usr/bin/lsblk", "-J", "-b", "-O"',
    '"/usr/bin/findmnt", "-no"',
    '"TARGET,SOURCE,FSTYPE"',
    '" 2>/dev/null"',
    '" 2>&1"',
]

for needle in required:
    if needle not in text:
        fail(f"missing required non-root probe argv marker: {needle}")

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

for sig, popen_text in [
    ("static int run_capture(", "popen(cmd.c_str()"),
    ("static bool run_cmd_capture(", "popen(cmd2.c_str()"),
]:
    body = function_body(sig)
    if not body:
        continue

    call = body.find("raid_try_run_nonroot_probe_argv")
    popen = body.find(popen_text)

    if call < 0:
        fail(f"{sig} missing non-root probe argv routing")
    if popen < 0:
        fail(f"{sig} expected legacy popen fallback not found")
    if call >= 0 and popen >= 0 and call > popen:
        fail(f"{sig} non-root probe argv routing appears after popen fallback")

helper_body = function_body("static bool raid_try_run_nonroot_probe_argv(")
if helper_body:
    for bad in [
        "popen(",
        "system(",
        "/bin/sh",
    ]:
        if bad in helper_body:
            fail(f"non-root probe argv helper must not use shell primitive: {bad}")

if failed:
    sys.exit(1)

print("OK: RAID non-root lsblk/findmnt probes route through argv before popen fallback.")
print("OK: legacy popen fallback remains only after explicit argv probe handling.")
PY
