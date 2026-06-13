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

cargo = read("tools/opaque_helper_rust/Cargo.toml")

old_deps = """[dependencies]
"""

new_deps = """[dependencies]
# Exact pin by policy: do not float OPAQUE crypto dependency versions.
opaque-ke = "=4.1.0-pre.0"
"""

replace_once("tools/opaque_helper_rust/Cargo.toml", old_deps, new_deps)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """use std::env;
use std::process;
""",
    """use std::env;
use std::process;

// Build-time dependency check only.
//
// The helper intentionally does not execute real OPAQUE protocol operations yet.
// This import makes the selected crate part of the Rust helper build while all
// protocol commands still fail closed.
use opaque_ke as _;
""",
)

replace_once(
    "docs/technical/opaque_login_design.md",
    """- Experimental Rust helper scaffold exists under `tools/opaque_helper_rust/`; it currently supports only `--version` and `self-test`, while future OPAQUE operations fail closed.
""",
    """- Experimental Rust helper scaffold exists under `tools/opaque_helper_rust/`; it currently supports only `--version` and `self-test`, while future OPAQUE operations fail closed.
- Rust helper pins `opaque-ke` as a build dependency, but does not execute production OPAQUE protocol operations yet.
""",
)

print("done")
