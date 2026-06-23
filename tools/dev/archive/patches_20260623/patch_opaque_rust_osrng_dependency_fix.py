#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def p(rel: str) -> Path:
    return ROOT / rel

def read(rel: str) -> str:
    path = p(rel)
    if not path.exists():
        die(f"missing file: {rel}")
    return path.read_text(encoding="utf-8")

def write(rel: str, text: str) -> None:
    p(rel).write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

replace_once(
    "tools/opaque_helper_rust/Cargo.toml",
    """[dependencies]
# Exact pins by policy: do not float OPAQUE crypto dependency versions.
opaque-ke = { version = "=4.1.0-pre.0", features = ["argon2", "ristretto255"] }
sha2 = "=0.10.9"
""",
    """[dependencies]
# Exact pins by policy: do not float OPAQUE crypto dependency versions.
opaque-ke = { version = "=4.1.0-pre.0", features = ["argon2", "ristretto255"] }
rand = { version = "=0.8.6", features = ["getrandom"] }
sha2 = "=0.10.9"
""",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    "use opaque_ke::rand::rngs::OsRng;\n",
    "use rand::rngs::OsRng;\n",
)

print("done")
