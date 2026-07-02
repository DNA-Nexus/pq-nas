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

start_marker = "const bool no_btrfs_matches = (rc != 0 && trim_copy(mounts_out).empty());"
end_marker = "std::vector<std::string> runtime_member_parents ="

start = text.find(start_marker)
end = text.find(end_marker, start if start >= 0 else 0)

if start < 0:
    fail("missing storage pools btrfs mount listing section start")
    section = ""
elif end < 0:
    fail("missing storage pools btrfs mount listing section end")
    section = ""
else:
    section = text[start:end]

required = [
    'run_btrfs_status_helper_capture("filesystem-show", target, &show_out)',
    'run_btrfs_status_helper_capture("filesystem-df-bytes", target, &df_out)',
    'run_btrfs_status_helper_capture("filesystem-usage-bytes", target, &usage_out)',
    'mount targets cannot be shell-interpreted',
]

for needle in required:
    if needle not in section:
        fail(f"storage pools section missing direct argv helper marker: {needle}")

for bad in [
    "/usr/local/sbin/pqnas-btrfs-status filesystem-show",
    "/usr/local/sbin/pqnas-btrfs-status filesystem-df-bytes",
    "/usr/local/sbin/pqnas-btrfs-status filesystem-usage-bytes",
    "sudo -n /usr/local/sbin/pqnas-btrfs-status",
    "sh_quote(target)",
]:
    if bad in section:
        fail(f"storage pools section still contains legacy shell-string marker: {bad}")

if failed:
    sys.exit(1)

print("OK: storage pools btrfs-status probes use direct argv helper calls.")
print("OK: storage pools section no longer builds pqnas-btrfs-status shell strings.")
PY
