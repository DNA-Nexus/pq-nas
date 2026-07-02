#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

route = Path("server/src/routes/routes_storage_raid.cpp")
if not route.is_file():
    print(f"ERROR: missing route file: {route}")
    sys.exit(1)

text = route.read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

start_marker = "// ----- GET /api/v4/raid/discovery"
end_marker = "// ----- GET /api/v4/raid/balance-status"

start = text.find(start_marker)
end = text.find(end_marker, start if start >= 0 else 0)

if start < 0:
    fail("missing raid discovery section start")
    section = ""
elif end < 0:
    fail("missing raid discovery section end")
    section = ""
else:
    section = text[start:end]

if section:
    required = 'run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show)'
    if required not in section:
        fail(f"raid discovery missing direct argv helper call: {required}")

    if "resolved mount targets cannot be shell-interpreted" not in section:
        fail("raid discovery missing security comment for argv helper conversion")

    for bad in [
        "sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show",
        "/usr/local/sbin/pqnas-btrfs-status filesystem-show",
        "const std::string cmd_show =",
        "run_cmd_capture(cmd_show, &show_raw, &ec_show)",
        "sh_quote(resolved_mount)",
    ]:
        if bad in section:
            fail(f"raid discovery still contains legacy shell-string marker: {bad}")

if failed:
    sys.exit(1)

print("OK: raid discovery filesystem-show probe uses direct btrfs-status argv helper call.")
print("OK: raid discovery section no longer builds pqnas-btrfs-status filesystem-show shell strings.")
PY
