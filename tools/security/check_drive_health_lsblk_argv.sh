#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

drive = Path("server/src/drive_health.cc")
if not drive.is_file():
    print(f"ERROR: missing drive health file: {drive}")
    sys.exit(1)

text = drive.read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required = [
    "run_command_capture_argv({",
    '"/usr/bin/lsblk"',
    '"-J"',
    '"-d"',
    '"-b"',
    '"-o"',
    '"NAME,PATH,MODEL,SERIAL,SIZE,ROTA,TYPE,TRAN"',
    "call lsblk via argv directly",
]

for needle in required:
    if needle not in text:
        fail(f"missing drive_health lsblk argv marker: {needle}")

legacy = [
    "static bool run_command_capture(const std::string& cmd",
    "::popen(cmd.c_str()",
    'const std::string cmd = "lsblk -J -d -b -o NAME,PATH,MODEL,SERIAL,SIZE,ROTA,TYPE,TRAN"',
    "run_command_capture(cmd, &txt, &rc)",
]

for needle in legacy:
    if needle in text:
        fail(f"legacy drive_health shell marker remains: {needle}")

if failed:
    sys.exit(1)

print("OK: drive_health lsblk inventory uses argv execution.")
print("OK: drive_health shell command capture helper is gone.")
PY
