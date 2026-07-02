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

required = [
    "main_run_argv_capture_no_shell",
    '"/usr/bin/findmnt", "-rn", "-t", "btrfs", "-o", "TARGET,SOURCE,FSTYPE"',
    "call findmnt via argv, not a shell string, when locating managed pools",
]

for needle in required:
    if needle not in text:
        fail(f"missing main btrfs mount-list argv marker: {needle}")

legacy = 'run_capture("/usr/bin/findmnt -rn -t btrfs -o TARGET,SOURCE,FSTYPE", &mounts_out)'
if legacy in text:
    fail("legacy main.cpp findmnt btrfs mount-list shell-string call remains")

if failed:
    sys.exit(1)

print("OK: main.cpp managed-pool btrfs mount listing uses argv execution.")
print("OK: legacy main.cpp findmnt btrfs shell-string list call is gone.")
PY
