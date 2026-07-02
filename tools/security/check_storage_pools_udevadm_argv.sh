#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

text = Path("server/src/storage_pools.cc").read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required = [
    "run_argv_capture_limited",
    '"/usr/bin/udevadm"',
    "Security: execute storage pool probes as argv; device paths never reach a shell.",
]

for marker in required:
    if marker not in text:
        fail(f"missing storage_pools argv marker: {marker}")

legacy = [
    "shell_quote_single",
    "run_command_capture_limited",
    "popen(",
    "pclose(",
    '"udevadm info --query=property --name="',
    " 2>/dev/null",
]

for marker in legacy:
    if marker in text:
        fail(f"legacy storage_pools shell marker remains: {marker}")

if failed:
    sys.exit(1)

print("OK: storage_pools udevadm probe uses argv execution.")
print("OK: storage_pools shell-backed capture helper is gone.")
PY
