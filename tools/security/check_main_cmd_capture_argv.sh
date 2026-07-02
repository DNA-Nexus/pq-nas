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
    "static bool run_cmd_capture_argv(const std::vector<std::string>& argv_s",
    "::execv(argv_s[0].c_str(), argv.data());",
    "Security: execute the requested command as argv, not via shell quoting.",
]

for marker in required:
    if marker not in text:
        fail(f"missing main argv command-capture marker: {marker}")

legacy = [
    "static bool run_cmd_capture(const std::string& cmd, std::string* out, int* exit_code)",
    "static std::string shell_quote_posix",
    "std::string cmd2 = cmd;",
    "popen(cmd2.c_str()",
    "return run_cmd_capture(cmd, out, exit_code)",
]

for marker in legacy:
    if marker in text:
        fail(f"legacy shell-backed command-capture marker remains: {marker}")

if failed:
    sys.exit(1)

print("OK: main.cpp run_cmd_capture_argv uses execv argv execution.")
print("OK: main.cpp shell-backed run_cmd_capture helper is gone.")
PY
