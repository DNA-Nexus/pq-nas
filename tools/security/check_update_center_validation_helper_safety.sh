#!/usr/bin/env bash
set -euo pipefail

ROUTES="server/src/updates/update_center_routes.cpp"

python3 - <<'PY'
from pathlib import Path
import re
import sys

routes = Path("server/src/updates/update_center_routes.cpp")
if not routes.is_file():
    print(f"ERROR: missing route file: {routes}")
    sys.exit(1)

text = routes.read_text()

bad_substrings = [
    ("timeout 30", "validation-only helper must not use shell timeout"),
    ("timeout 60", "dry-run helper must not use shell timeout"),
    ("--validation-only 2>&1", "validation-only helper must not use shell redirection"),
    ("--dry-run 2>&1", "dry-run helper must not use shell redirection"),
    ("update_shell_quote(helper_path)", "helper_path must not be shell-quoted for execution"),
]

failed = False
for needle, message in bad_substrings:
    if needle in text:
        print(f"ERROR: {message}: found {needle!r}")
        failed = True

bad_patterns = [
    (
        r"update_run_command_limited\(\s*cmd,\s*2u \* 1024u \* 1024u,\s*&helper_status\s*\)",
        "validation-only helper still uses shell command runner",
    ),
    (
        r"update_run_command_limited\(\s*cmd,\s*4u \* 1024u \* 1024u,\s*&helper_status\s*\)",
        "dry-run helper still uses shell command runner",
    ),
]

for pattern, message in bad_patterns:
    if re.search(pattern, text, re.S):
        print(f"ERROR: {message}")
        failed = True

required_patterns = [
    (
        r"std::string\s+update_apply_helper_path\s*\(",
        "missing fixed update_apply_helper_path helper",
    ),
    (
        r"update_run_argv_limited\(\s*\{\s*\"/usr/bin/python3\",\s*helper_path,\s*\"--plan-id\",\s*plan_id,\s*\"--validation-only\"\s*\}",
        "validation-only helper is not run through argv with fixed /usr/bin/python3",
    ),
    (
        r"update_run_argv_limited\(\s*\{\s*\"/usr/bin/python3\",\s*helper_path,\s*\"--plan-id\",\s*plan_id,\s*\"--dry-run\"\s*\}",
        "dry-run helper is not run through argv with fixed /usr/bin/python3",
    ),
]

for pattern, message in required_patterns:
    if not re.search(pattern, text, re.S):
        print(f"ERROR: {message}")
        failed = True

if failed:
    sys.exit(1)

print("OK: Update Center validation-only/dry-run helper calls use argv execution.")
print("OK: helper_path is pinned to the fixed root-managed update helper.")
print("OK: shell timeout/redirection/helper_path quoting regressions are blocked.")
PY
