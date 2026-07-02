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
    "run_findmnt_btrfs_list_argv",
    'fields != "TARGET" && fields != "TARGET,SOURCE,FSTYPE"',
    '{"/usr/bin/findmnt", "-rn", "-t", "btrfs", "-o", fields}',
    'run_findmnt_btrfs_list_argv("TARGET,SOURCE,FSTYPE", &mounts_out);',
    'run_findmnt_btrfs_list_argv("TARGET", &mounts_out);',
    "call findmnt via argv directly",
]

for needle in required:
    if needle not in text:
        fail(f"missing findmnt btrfs argv marker: {needle}")

legacy = re.findall(r'"/usr/bin/findmnt -rn -t btrfs -o ', text)
if legacy:
    fail(f"legacy findmnt -rn btrfs shell-string call sites remain: {len(legacy)}")

target_no_legacy = re.findall(r'"/usr/bin/findmnt -no (TARGET|FSTYPE|SOURCE) --target "', text)
if target_no_legacy:
    fail(f"legacy findmnt -no target shell-string call sites returned: {len(target_no_legacy)}")

if failed:
    sys.exit(1)

print("OK: findmnt -rn btrfs list probes use direct argv helper.")
print("OK: no legacy findmnt shell-string probe call sites remain.")
PY
