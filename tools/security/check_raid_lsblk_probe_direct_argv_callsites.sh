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
    "run_lsblk_json_all_props_argv",
    'std::vector<std::string> argv{',
    '"/usr/bin/lsblk",',
    '"-J",',
    '"-b",',
    '"-O"',
    "raid_probe_abs_path_arg_is_safe(disk_path)",
    'run_lsblk_json_all_props_argv("", &out);',
    "int rc = run_lsblk_json_all_props_argv(disk_path, &raw);",
    "call lsblk via argv directly",
]

for needle in required:
    if needle not in text:
        fail(f"missing lsblk argv marker: {needle}")

legacy = [
    'run_capture("lsblk -J -b -O 2>/dev/null", &out);',
    'run_capture("/usr/bin/lsblk -J -b -O " + sh_quote(disk_path) + " 2>/dev/null", &raw);',
]

for bad in legacy:
    if bad in text:
        fail(f"legacy lsblk shell-string call site still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: storage lsblk probe call sites use direct argv helper.")
print("OK: legacy lsblk shell-string call sites are gone.")
PY
