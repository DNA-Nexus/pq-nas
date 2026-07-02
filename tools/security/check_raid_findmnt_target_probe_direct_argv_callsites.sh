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
    "run_findmnt_no_target_argv",
    'field != "TARGET" && field != "FSTYPE" && field != "SOURCE"',
    "raid_probe_abs_path_arg_is_safe(target)",
    '{"/usr/bin/findmnt", "-no", field, "--target", target}',
    'run_findmnt_no_target_argv("TARGET", mount, &fs_target_out);',
    'run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);',
    'run_findmnt_no_target_argv("SOURCE", mount, &source_out);',
    'run_findmnt_no_target_argv("TARGET", mount, &target_out);',
    "call findmnt via argv directly",
]

for needle in required:
    if needle not in text:
        fail(f"missing findmnt argv marker: {needle}")

legacy = [
    'int rc_target = run_capture("/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &fs_target_out);',
    'int rc_target = run_capture("/usr/bin/findmnt -no TARGET --target " + sh_quote(mount), &target_out);',
]

for bad in legacy:
    if bad in text:
        fail(f"legacy selected findmnt shell-string call site still exists: {bad}")

if failed:
    sys.exit(1)

print("OK: selected findmnt TARGET/FSTYPE/SOURCE probe call sites use direct argv helper.")
print("OK: selected legacy findmnt shell-string call sites are gone.")
PY
