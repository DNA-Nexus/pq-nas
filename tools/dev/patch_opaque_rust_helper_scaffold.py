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
    path = p(rel)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

write("tools/opaque_helper_rust/Cargo.toml", """[package]
name = "pqnas_opaque_helper"
version = "0.1.0"
edition = "2021"
rust-version = "1.85"
publish = false

[dependencies]
""")

write("tools/opaque_helper_rust/src/main.rs", r'''use std::env;
use std::process;

const PROGRAM_NAME: &str = "pqnas_opaque_helper";
const VERSION: &str = "0.1.0-rust-scaffold";

fn is_future_opaque_op(op: &str) -> bool {
    matches!(
        op,
        "server-setup-create"
            | "register-start"
            | "register-finish"
            | "login-start"
            | "login-finish"
    )
}

fn print_version() -> i32 {
    println!("{PROGRAM_NAME} {VERSION}");
    0
}

fn run_self_test() -> i32 {
    println!("ok: {PROGRAM_NAME} rust scaffold self-test passed");
    0
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());

    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => {
                use std::fmt::Write;
                let _ = write!(&mut out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }

    out
}

fn fail_closed_not_implemented(op: &str) -> i32 {
    println!(
        "{{\"ok\":false,\"op\":\"{}\",\"error\":\"opaque_backend_not_implemented\",\"message\":\"OPAQUE Rust helper scaffold only; no production OPAQUE crypto is available yet\"}}",
        json_escape(op)
    );
    1
}

fn print_usage() -> i32 {
    eprintln!(
        "Usage:\n  {PROGRAM_NAME} --version\n  {PROGRAM_NAME} self-test\n\nFuture protocol operations are recognized but fail closed:\n  server-setup-create\n  register-start\n  register-finish\n  login-start\n  login-finish"
    );
    2
}

fn main() {
    let mut args = env::args();
    let _program = args.next();

    let Some(arg) = args.next() else {
        process::exit(print_usage());
    };

    if args.next().is_some() {
        process::exit(print_usage());
    }

    let rc = match arg.as_str() {
        "--version" | "version" => print_version(),
        "self-test" => run_self_test(),
        op if is_future_opaque_op(op) => fail_closed_not_implemented(op),
        _ => {
            eprintln!("unknown command: {arg}");
            print_usage()
        }
    };

    process::exit(rc);
}
''')

replace_once(
    "CMakeLists.txt",
    """add_custom_target(run_pqnas_opaque_helper_self_test
        COMMAND "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper" self-test
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper
    )


# -----------------------------------------------------------------------------
# Test: test_opaque_helper_client
# -----------------------------------------------------------------------------
""",
    """add_custom_target(run_pqnas_opaque_helper_self_test
        COMMAND "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper" self-test
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper
    )

# -----------------------------------------------------------------------------
# Tool: pqnas_opaque_helper_rust
#
# Experimental Rust helper scaffold only:
# - not part of the default ALL build
# - does not replace the current C++ pqnas_opaque_helper yet
# - no production OPAQUE cryptography yet
# - future protocol operations fail closed
# -----------------------------------------------------------------------------
find_program(PQNAS_CARGO cargo)

if (PQNAS_CARGO)
    set(PQNAS_OPAQUE_RUST_DIR "${CMAKE_SOURCE_DIR}/tools/opaque_helper_rust")
    set(PQNAS_OPAQUE_RUST_BIN "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper_rust")

    add_custom_target(pqnas_opaque_helper_rust
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${PQNAS_CARGO} build --manifest-path "${PQNAS_OPAQUE_RUST_DIR}/Cargo.toml"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PQNAS_OPAQUE_RUST_DIR}/target/debug/pqnas_opaque_helper"
            "${PQNAS_OPAQUE_RUST_BIN}"
        BYPRODUCTS "${PQNAS_OPAQUE_RUST_BIN}"
        WORKING_DIRECTORY "${PQNAS_OPAQUE_RUST_DIR}"
        COMMENT "Building experimental Rust OPAQUE helper scaffold"
        VERBATIM
    )

    add_custom_target(run_pqnas_opaque_helper_rust_self_test
        COMMAND "${PQNAS_OPAQUE_RUST_BIN}" self-test
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper_rust
    )
else()
    message(WARNING "cargo not found; pqnas_opaque_helper_rust target will not be available")
endif()


# -----------------------------------------------------------------------------
# Test: test_opaque_helper_client
# -----------------------------------------------------------------------------
""",
)

replace_once(
    "docs/technical/opaque_login_design.md",
    "- Selected server-side implementation direction: Rust helper binary using `opaque-ke`.\n",
    "- Selected server-side implementation direction: Rust helper binary using `opaque-ke`.\n"
    "- Experimental Rust helper scaffold exists under `tools/opaque_helper_rust/`; it currently supports only `--version` and `self-test`, while future OPAQUE operations fail closed.\n",
)

print("done")
