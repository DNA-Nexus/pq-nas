#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

paths = [
    Path("server/src/main.cpp"),
    Path("server/src/routes/routes_snapshots_restore.h"),
    Path("server/src/routes/routes_snapshots_restore.cpp"),
    Path("server/src/routes/routes_files_put.inc"),
]

text = "\n".join(p.read_text() for p in paths)
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

legacy = [
    "static bool popen_capture(",
    "popen_capture",
    "popen(cmd.c_str()",
    "pclose(fp)",
    "FILE* fp = popen(",
]

for marker in legacy:
    if marker in text:
        fail(f"legacy snapshot restore shell-capture marker remains: {marker}")

required = [
    "run_restore_root_argv(",
    "run_restore_root_ctx(",
    "Security: helper builds pqnas-restore@<job_id>.service",
]

for marker in required:
    if marker not in text:
        fail(f"missing snapshot restore argv marker: {marker}")

if failed:
    sys.exit(1)

print("OK: snapshot restore no longer exposes popen_capture.")
print("OK: snapshot restore still uses pqnas-restore-root argv helpers.")
PY
