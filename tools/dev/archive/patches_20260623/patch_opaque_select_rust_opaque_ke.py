#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
FENCE = "`" * 3

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

doc = "docs/technical/opaque_login_design.md"

replace_once(
    doc,
    "- Admin Settings UI includes an OPAQUE Status card that displays the admin-only backend diagnostics without enabling public OPAQUE login.\n"
    "- Existing QR login, classic password login, mobile pairing, and app token logic are intentionally unchanged.\n",
    "- Admin Settings UI includes an OPAQUE Status card that displays the admin-only backend diagnostics without enabling public OPAQUE login.\n"
    "- Selected server-side implementation direction: Rust helper binary using `opaque-ke`.\n"
    "- Existing QR login, classic password login, mobile pairing, and app token logic are intentionally unchanged.\n",
)

old_strategy = "\n".join([
    "Preferred strategy:",
    "",
    FENCE + "text",
    "pqnas_server C++ routes",
    "  -> small OPAQUE integration wrapper",
    "  -> isolated Rust helper or C ABI library",
    "  -> reviewed OPAQUE implementation",
    FENCE,
    "",
])

new_strategy = "\n".join([
    "Selected strategy:",
    "",
    FENCE + "text",
    "pqnas_server C++ routes",
    "  -> small OPAQUE integration wrapper",
    "  -> local Rust helper binary",
    "  -> opaque-ke",
    FENCE,
    "",
    "The helper boundary remains process-based for the first implementation. The C++",
    "server must not link directly to OPAQUE crypto until the helper protocol,",
    "serialization, and test vectors are stable.",
    "",
])

replace_once(doc, old_strategy, new_strategy)

old_section = "\n".join([
    "## Candidate library direction",
    "",
    "Primary candidate:",
    "",
    FENCE + "text",
    "Rust opaque-ke",
    FENCE,
    "",
    "Reasons:",
    "",
    "- implements the RFC 9807 OPAQUE protocol",
    "- supports OPAQUE registration and login flows",
    "- has existing Rust ecosystem support",
    "- can be wrapped behind a helper binary or C ABI",
    "- has WASM-related ecosystem options for browser/mobile experiments",
    "",
    "Important rule:",
    "",
    FENCE + "text",
    "No production OPAQUE mode until the chosen library, suite, serialization format, and browser-client story are explicitly documented.",
    FENCE,
    "",
])

new_section = "\n".join([
    "## Selected library direction",
    "",
    "Selected server-side library family:",
    "",
    FENCE + "text",
    "Rust opaque-ke",
    FENCE,
    "",
    "Reasons:",
    "",
    "- implements OPAQUE using a maintained Rust library instead of custom C++ crypto",
    "- is based on the RFC 9807 OPAQUE specification",
    "- can be isolated behind the existing `pqnas_opaque_helper` process boundary",
    "- keeps `pqnas_server` responsible for HTTP, users, audit, storage, and session minting only",
    "- keeps the OPAQUE library replaceable behind a stable helper JSON contract",
    "",
    "Version policy:",
    "",
    FENCE + "text",
    "Pin the exact opaque-ke crate version in the Rust helper Cargo.toml.",
    "Do not float the dependency in production builds.",
    "Do not switch OPAQUE ciphersuite/serialization without a migration plan.",
    FENCE,
    "",
    "Browser-client policy:",
    "",
    FENCE + "text",
    "The browser OPAQUE client implementation is not selected yet.",
    "The server helper may use opaque-ke first, while the browser side can later use a compatible WASM/JS OPAQUE package.",
    FENCE,
    "",
    "Important rule:",
    "",
    FENCE + "text",
    "No production OPAQUE mode until the selected crate version, OPAQUE suite, serialized message formats, server setup format, credential format, and browser-client implementation are explicitly documented and tested together.",
    FENCE,
    "",
])

replace_once(doc, old_section, new_section)

old_credentials = "\n".join([
    "Recommended new file:",
    "",
    FENCE + "text",
    "/etc/pqnas/opaque_credentials.json",
    FENCE,
    "",
])

new_credentials = "\n".join([
    "Resolved runtime file:",
    "",
    FENCE + "text",
    "${PQNAS_CONFIG_ROOT or PQNAS_CONFIG or /etc/pqnas}/opaque_credentials.json",
    FENCE,
    "",
    "On the current dev deployment this resolves to:",
    "",
    FENCE + "text",
    "/srv/pqnas/config/opaque_credentials.json",
    FENCE,
    "",
])

replace_once(doc, old_credentials, new_credentials)

old_setup = "\n".join([
    "Recommended file:",
    "",
    FENCE + "text",
    "/etc/pqnas/opaque_server_setup.bin",
    FENCE,
    "",
])

new_setup = "\n".join([
    "Resolved runtime file:",
    "",
    FENCE + "text",
    "${PQNAS_CONFIG_ROOT or PQNAS_CONFIG or /etc/pqnas}/opaque_server_setup.bin",
    FENCE,
    "",
    "On the current dev deployment this resolves to:",
    "",
    FENCE + "text",
    "/srv/pqnas/config/opaque_server_setup.bin",
    FENCE,
    "",
])

replace_once(doc, old_setup, new_setup)

print("done")
