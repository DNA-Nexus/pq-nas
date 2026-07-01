#!/usr/bin/env bash
set -euo pipefail

ROUTES="server/src/routes/routes_storage_raid.cpp"

test -f "$ROUTES" || {
  echo "ERROR: missing RAID route file: $ROUTES"
  exit 1
}

python3 - <<'PY'
from pathlib import Path
import sys

p = Path("server/src/routes/routes_storage_raid.cpp")
s = p.read_text(encoding="utf-8")

def fail(msg: str) -> None:
    print("ERROR:", msg)
    sys.exit(1)

if "raid_try_run_known_root_helper_argv" not in s:
    fail("missing known root-helper argv interceptor")

if "raid_parse_legacy_helper_tail" not in s:
    fail("missing strict legacy helper command parser")

def body_after(signature: str) -> str:
    start = s.find(signature)
    if start < 0:
        fail(f"function signature not found: {signature}")

    brace = s.find("{", start)
    if brace < 0:
        fail(f"opening brace not found: {signature}")

    depth = 0
    for i in range(brace, len(s)):
        if s[i] == "{":
            depth += 1
        elif s[i] == "}":
            depth -= 1
            if depth == 0:
                return s[brace:i + 1]

    fail(f"closing brace not found: {signature}")
    return ""

for sig, popen_text in [
    ("static int run_capture(", "popen(cmd.c_str()"),
    ("static bool run_cmd_capture(", "popen(cmd2.c_str()"),
]:
    body = body_after(sig)
    call = body.find("raid_try_run_known_root_helper_argv(cmd")
    popen = body.find(popen_text)

    if popen < 0:
        fail(f"expected popen fallback not found in {sig}")

    if call < 0:
        fail(f"{sig} does not route known root-helper commands to argv before shell fallback")

    if call > popen:
        fail(f"{sig} root-helper argv routing appears after popen fallback")

print("OK: RAID route intercepts known root-helper command strings before shell fallback.")
PY

if [ -x tools/security/check_raid_helper_safety.sh ]; then
  tools/security/check_raid_helper_safety.sh >/dev/null
fi

echo "OK: RAID plan/executor root-helper argv guard passed."
