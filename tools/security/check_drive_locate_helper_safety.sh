#!/usr/bin/env bash
set -euo pipefail

python3 - <<'PY'
from pathlib import Path
import re
import sys

helper = Path("server/src/storage/pqnas_drive_locate_root.sh")
route = Path("server/src/routes/routes_drive_locate.cpp")
installer = Path("tools/installer/pqnas_install.py")

failed = False

def err(msg: str) -> None:
    global failed
    print(f"ERROR: {msg}")
    failed = True

if not helper.is_file():
    err(f"missing drive locate helper: {helper}")
if not route.is_file():
    err(f"missing drive locate route: {route}")
if not installer.is_file():
    err(f"missing installer: {installer}")

if failed:
    sys.exit(1)

h = helper.read_text()
r = route.read_text()
i = installer.read_text()

required_helper = [
    ('#!/bin/bash', "helper must use fixed /bin/bash shebang"),
    ('unset BASH_ENV ENV CDPATH', "helper must clear caller shell environment"),
    ('export PATH="/usr/sbin:/usr/bin:/sbin:/bin"', "helper must set fixed PATH"),
    ('umask 022', "helper must set root-helper umask"),
    ('AWK="/usr/bin/awk"', "helper must define absolute awk path"),
    ('LSBLK="/usr/bin/lsblk"', "helper must define absolute lsblk path"),
    ('READLINK="/usr/bin/readlink"', "helper must define absolute readlink path"),
    ('SMARTCTL="/usr/sbin/smartctl"', "helper must define absolute smartctl path"),
    ('SSH="/usr/bin/ssh"', "helper must define absolute ssh path"),
    ('TIMEOUT="/usr/bin/timeout"', "helper must define absolute timeout path"),
    ('SSH_KEYGEN="/usr/bin/ssh-keygen"', "helper must define absolute ssh-keygen path"),
    ('"$TIMEOUT" 45 "$SSH"', "iDRAC SSH must use absolute timeout and ssh variables"),
    ('"$SMARTCTL" -i "$CANON"', "smartctl must use absolute path variable"),
    ('"$SSH_KEYGEN" -t rsa', "ssh-keygen must use absolute path variable"),
    ('"$INSTALL" -d -m 0755', "install must use absolute path variable"),
    ('"$CHMOD" 0600', "chmod must use absolute path variable"),
    ('"$CHOWN" root:root', "chown must use absolute path variable"),
]

for needle, msg in required_helper:
    if needle not in h:
        err(msg)

bad_helper = [
    ('#!/usr/bin/env bash', "helper must not use /usr/bin/env bash"),
    ('command -v ', "helper must not use PATH-based command discovery"),
    ('timeout 45 ssh', "helper must not run timeout/ssh through PATH"),
    ('smartctl -i', "helper must not run smartctl through PATH"),
    ('ssh-keygen -t', "helper must not run ssh-keygen through PATH"),
    ('install -d', "helper must not run install through PATH"),
    ('chmod 0600', "helper must not run chmod through PATH"),
    ('chown root:root', "helper must not run chown through PATH"),
    ('readlink -f', "helper must not run readlink through PATH"),
    ('lsblk -dn', "helper must not run lsblk through PATH"),
    ('lspci -nn', "helper must not run lspci through PATH"),
    (' | head ', "helper must not run head through PATH"),
    (' | tr ', "helper must not run tr through PATH"),
    (' | grep ', "helper must not run grep through PATH"),
]

for needle, msg in bad_helper:
    if needle in h:
        err(f"{msg}: found {needle!r}")

required_route = [
    ('execv("/usr/bin/sudo"', "route must use fixed /usr/bin/sudo argv execution"),
    ('argv.push_back(const_cast<char*>("-n"))', "route must pass sudo -n as argv"),
    ('"/usr/local/sbin/pqnas-drive-locate"', "route must default to fixed drive-locate helper path"),
]

for needle, msg in required_route:
    if needle not in r:
        err(msg)

bad_route_patterns = [
    (r'\bpopen\s*\(', "route must not use popen for drive locate"),
    (r'\bsystem\s*\(', "route must not use system for drive locate"),
    (r'sudo -n .*pqnas-drive-locate', "route must not build sudo shell strings for drive locate"),
    (r'/bin/sh\s+-c', "route must not use /bin/sh -c"),
]

for pattern, msg in bad_route_patterns:
    if re.search(pattern, r):
        err(msg)

if 'pqnas ALL=(root) NOPASSWD: /usr/local/sbin/pqnas-drive-locate *' not in i:
    err("installer must keep sudoers scoped to pqnas-drive-locate wrapper")

# Security: block direct sudoers grants to backend tools, but allow wrapper
# sudoers such as /usr/local/sbin/pqnas-drive-locate and /usr/local/sbin/pqnas-smartctl.
direct_backend_sudoers = re.compile(
    r'NOPASSWD: .*?/(?:usr/)?(?:sbin|bin)/(?:ledctl|sg_ses|hdparm|smartctl|ssh|ssh-keygen|chmod|chown|install)\\b'
)
for line in i.splitlines():
    if "NOPASSWD:" not in line:
        continue
    if direct_backend_sudoers.search(line):
        err("installer must not grant direct sudoers access to drive-locate backend tools")

if failed:
    sys.exit(1)

print("OK: drive locate helper uses fixed shebang, clean env, fixed PATH, and umask.")
print("OK: drive locate helper uses absolute tool paths for privileged backend commands.")
print("OK: drive locate route uses fixed /usr/bin/sudo argv execution.")
print("OK: drive locate sudoers remains wrapper-scoped.")
PY
