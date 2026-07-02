#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

text = Path("server/src/main.cpp").read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required = [
    "static int public_share_run_argv_wait_local",
    "Security: execute ffmpeg with argv; shared video paths never reach a shell.",
    "::execv(argv_s[0].c_str(), argv.data());",
    "scale=1200:-2:force_original_aspect_ratio=decrease",
]

for marker in required:
    if marker not in text:
        fail(f"missing ffmpeg argv marker: {marker}")

legacy = [
    "public_share_shell_quote_local",
    "std::ostringstream cmd;",
    "std::system(cmd.str().c_str())",
]

for marker in legacy:
    if marker in text:
        fail(f"legacy ffmpeg shell marker remains: {marker}")

if failed:
    sys.exit(1)

print("OK: main.cpp ffmpeg poster generation uses argv execution.")
print("OK: ffmpeg shell command construction is gone.")
PY
