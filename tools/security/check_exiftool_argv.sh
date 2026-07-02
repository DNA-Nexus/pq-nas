#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

main = Path("server/src/main.cpp").read_text()
meta = Path("server/src/image_embedded_meta.cpp").read_text()

failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required_meta = [
    "run_argv_capture_stdout_local",
    '"/usr/bin/exiftool"',
    "Security: execute exiftool with argv; image paths never reach a shell.",
]

for marker in required_meta:
    if marker not in meta:
        fail(f"missing image_embedded_meta argv marker: {marker}")

legacy_meta = [
    "shell_quote_single_local",
    "run_command_capture_stdout_local",
    "::popen",
    "::pclose",
    '"exiftool -j -n -G1 ',
    " 2>/dev/null",
]

for marker in legacy_meta:
    if marker in meta:
        fail(f"legacy image_embedded_meta shell marker remains: {marker}")

required_main = [
    "read_command_stdout_argv_silent_stderr",
    '"/usr/bin/exiftool"',
    "Security: execute exiftool with argv; photo paths never reach a shell.",
]

for marker in required_main:
    if marker not in main:
        fail(f"missing main exiftool argv marker: {marker}")

legacy_main = [
    "std::string read_command_stdout(const std::string& cmd)",
    '"exiftool -json -n ',
    "+ shell_quote_single(abs_path.string())",
]

for marker in legacy_main:
    if marker in main:
        fail(f"legacy main exiftool shell marker remains: {marker}")

if failed:
    sys.exit(1)

print("OK: exiftool metadata reads use argv execution.")
print("OK: legacy exiftool shell command strings are gone.")
PY
