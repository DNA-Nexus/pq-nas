#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import re
import sys

route = Path("server/src/routes/routes_storage_raid.cpp")
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
    'run_findmnt_no_target_argv("SOURCE", root, &source_out);',
    'run_findmnt_no_target_argv("FSTYPE", allowed_prefix, &root_fstype);',
    'run_findmnt_no_target_argv("TARGET", mount, &target_out);',
    'run_findmnt_no_target_argv("FSTYPE", mount, &fstype_out);',
    'run_findmnt_no_target_argv("SOURCE", mount, &source_out);',
]

for needle in required:
    if needle not in text:
        fail(f"missing bulk findmnt argv marker: {needle}")

legacy_no = re.findall(r'"/usr/bin/findmnt -no (TARGET|FSTYPE|SOURCE) --target "', text)
if legacy_no:
    fail(f"legacy findmnt -no TARGET/FSTYPE/SOURCE shell-string call sites remain: {len(legacy_no)}")

argv_calls = len(re.findall(r'run_findmnt_no_target_argv\("(TARGET|FSTYPE|SOURCE)"', text))
if argv_calls < 30:
    fail(f"expected at least 30 findmnt argv call sites after bulk migration, found {argv_calls}")

rn_calls = len(re.findall(r'"/usr/bin/findmnt -rn -t btrfs -o ', text))
if rn_calls != 2:
    fail(f"expected exactly 2 remaining findmnt -rn btrfs shell-string call sites for later PR, found {rn_calls}")

if failed:
    sys.exit(1)

print("OK: bulk findmnt TARGET/FSTYPE/SOURCE probe call sites use direct argv helper.")
print("OK: no legacy findmnt -no TARGET/FSTYPE/SOURCE shell-string call sites remain.")
print("OK: findmnt -rn btrfs listing probes are still isolated for a later PR.")
PY
