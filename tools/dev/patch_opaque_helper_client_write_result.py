#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
path = ROOT / "server/src/opaque_helper_client.cpp"

old = """        (void)::write(STDERR_FILENO, msg.data(), msg.size());
        _exit(127);
"""

new = """        const ssize_t write_rc = ::write(STDERR_FILENO, msg.data(), msg.size());
        (void)write_rc;
        _exit(127);
"""

if not path.exists():
    print(f"ERROR: missing file: {path}", file=sys.stderr)
    sys.exit(1)

text = path.read_text(encoding="utf-8")

if old not in text:
    if new in text:
        print("unchanged: write result already handled")
        sys.exit(0)
    print("ERROR: write-result anchor not found in server/src/opaque_helper_client.cpp", file=sys.stderr)
    sys.exit(1)

path.write_text(text.replace(old, new, 1), encoding="utf-8")
print("patched: server/src/opaque_helper_client.cpp")
