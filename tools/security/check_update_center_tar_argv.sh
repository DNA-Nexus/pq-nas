#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

text = Path("server/src/updates/update_center_routes.cpp").read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required = [
    "Security: read candidate manifests with tar argv; package paths never reach a shell.",
    "Security: list package contents with tar argv instead of shell command strings.",
    '"/usr/bin/tar"',
    "update_run_argv_limited(",
    "tar_result.exit_code",
]

for marker in required:
    if marker not in text:
        fail(f"missing update-center tar argv marker: {marker}")

legacy = [
    "std::string update_shell_quote",
    "std::string update_run_command_limited",
    "popen(",
    "pclose(",
    '"tar -xOzf "',
    '"tar -tzf "',
    "update_shell_quote(",
    "update_run_command_limited(",
    " 2>/dev/null",
    " 2>&1",
]

for marker in legacy:
    if marker in text:
        fail(f"legacy update-center shell marker remains: {marker}")

if failed:
    sys.exit(1)

print("OK: update-center tar probes use argv execution.")
print("OK: update-center shell-backed tar helper is gone.")
PY
