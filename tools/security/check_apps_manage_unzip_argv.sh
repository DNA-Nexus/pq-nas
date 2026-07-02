#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

cpp = Path("server/src/routes/routes_apps_manage.cpp").read_text()
hdr = Path("server/src/routes/routes_apps_manage.h").read_text()
inc = Path("server/src/routes/routes_files_put.inc").read_text()

failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required_cpp = [
    "run_argv_capture_limited",
    "run_unzip_capture",
    'argv.push_back("/usr/bin/unzip");',
    "Security: run unzip with fixed absolute binary and argv",
    'run_unzip_capture({"-Z1", tmpZip.string()}',
    'run_unzip_capture({"-p", tmpZip.string(), "manifest.json"}',
    'run_unzip_capture({"-q", tmpZip.string(), "-d", tmp.string()}',
    'run_unzip_capture({"-p", zip_path.string(), "manifest.json"}',
    'run_unzip_capture({"-q", zip_path.string(), "-d", tmp.string()}',
]

for marker in required_cpp:
    if marker not in cpp:
        fail(f"missing apps-manage argv unzip marker: {marker}")

legacy_cpp = [
    "g_apps_ctx.run_cmd_capture",
    "bool run_cmd_capture(const std::string& cmd, std::string* out, int* rc)",
    'const std::string cmd = "unzip ',
    "run_cmd_capture(cmd",
    " 2>/dev/null",
]

for marker in legacy_cpp:
    if marker in cpp:
        fail(f"legacy apps-manage shell/unzip marker remains: {marker}")

if "run_cmd_capture" in hdr:
    fail("routes_apps_manage.h still exposes run_cmd_capture")

if "apps_ctx.run_cmd_capture" in inc:
    fail("routes_files_put.inc still wires apps_ctx.run_cmd_capture")

if failed:
    sys.exit(1)

print("OK: apps-manage unzip uses argv execution.")
print("OK: apps-manage no longer depends on main.cpp run_cmd_capture.")
PY
