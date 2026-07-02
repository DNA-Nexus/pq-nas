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

legacy_plain = '''    const std::string cmd_show =
        "/usr/bin/sudo -n /usr/local/sbin/pqnas-btrfs-status filesystem-show " + sh_quote(resolved_mount);

    std::string show_raw;
    int ec_show = 0;
    // hardening: route pseudo commands through guarded runner.
    const bool ok_show = run_cmd_capture(cmd_show, &show_raw, &ec_show);
    cap_string(show_raw, 256 * 1024);
'''

if legacy_plain in text:
    fail("plain resolved_mount filesystem-show cmd_show shell-string block still exists")

helper_call = 'run_btrfs_status_helper_argv("filesystem-show", resolved_mount, &show_raw, &ec_show)'
helper_count = text.count(helper_call)
if helper_count < 7:
    fail(f"expected at least 7 resolved_mount filesystem-show argv helper calls after conversions, found {helper_count}")

if "resolved mount targets cannot be shell-interpreted" not in text:
    fail("missing security comment for resolved_mount argv helper conversion")

if failed:
    sys.exit(1)

print("OK: plain resolved_mount filesystem-show cmd_show shell-string blocks are gone.")
print("OK: resolved_mount filesystem-show probes use direct btrfs-status argv helper calls.")
PY
