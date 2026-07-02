#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import sys

main = Path("server/src/main.cpp")
text = main.read_text()
failed = False

def fail(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

required = [
    "main_run_argv_capture_no_shell",
    "::execv(argv_s[0].c_str(), argv.data());",
    '"/usr/bin/findmnt", "-rn", "-t", "btrfs", "-o", "TARGET"',
    '"/usr/bin/sudo", "-n", "/usr/local/sbin/pqnas-raid-root", "mkdir-p", mount',
    '"mount-spec", mount_spec, mount',
    '"/usr/bin/sudo", "-n", "/usr/local/sbin/pqnas-raid-root", "udev-settle"',
    '"/usr/bin/sudo", "-n", "/usr/local/sbin/pqnas-raid-root", "btrfs-device-scan"',
    "call findmnt via argv",
    "call pqnas-raid-root via sudo argv",
    "pool_mounts_restore_managed(users_path)",
]

for needle in required:
    if needle not in text:
        fail(f"missing startup restore argv marker: {needle}")

legacy = [
    'run_capture("/usr/bin/findmnt -rn -t btrfs -o TARGET", &mounts_out)',
    'run_capture("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root mkdir-p " + sh_quote(mount)',
    'run_capture("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root mount-spec "',
    'run_capture("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root udev-settle 2>&1", &out)',
    'run_capture("/usr/bin/sudo -n /usr/local/sbin/pqnas-raid-root btrfs-device-scan 2>&1", &out)',
]

for needle in legacy:
    if needle in text:
        fail(f"legacy startup restore shell marker remains: {needle}")

if failed:
    sys.exit(1)

print("OK: main.cpp startup pool restore uses argv execution.")
print("OK: legacy startup restore shell-string call sites are gone.")
PY
