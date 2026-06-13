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
# Exact pin by policy: do not float OPAQUE crypto dependency versions.
opaque-ke = "=4.1.0-pre.0"
""",
    """[dependencies]
# Exact pins by policy: do not float OPAQUE crypto dependency versions.
opaque-ke = { version = "=4.1.0-pre.0", features = ["argon2", "ristretto255"] }
sha2 = "=0.10.9"
""",
)

write("tools/opaque_helper_rust/src/main.rs", r'''use std::env;
use std::fs::OpenOptions;
use std::io::Write;
#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;
use std::process;

use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use opaque_ke::rand::rngs::OsRng;
use opaque_ke::{Ristretto255, ServerSetup, TripleDh};
use sha2::Sha512;

const PROGRAM_NAME: &str = "pqnas_opaque_helper";
const VERSION: &str = "0.1.0-rust-scaffold";

struct PqnasOpaqueCipherSuite;

impl CipherSuite for PqnasOpaqueCipherSuite {
    type OprfCs = Ristretto255;
    type KeyExchange = TripleDh<Ristretto255, Sha512>;
    type Ksf = Argon2<'static>;
}

fn is_future_opaque_op(op: &str) -> bool {
    matches!(
        op,
        "register-start" | "register-finish" | "login-start" | "login-finish"
    )
}

fn print_version() -> i32 {
    println!("{PROGRAM_NAME} {VERSION}");
    0
}

fn run_self_test() -> i32 {
    // This is intentionally still a scaffold smoke test.
    //
    // Security boundary:
    // - OPAQUE ServerSetup generation is available through server-setup-create
    // - no registration is implemented yet
    // - no login is implemented yet
    // - no password material is accepted
    // - no users.json access is performed
    // - no PQ-NAS session can be minted by this helper
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

fn print_json_error(op: &str, error: &str, message: &str) -> i32 {
    println!(
        "{{\"ok\":false,\"op\":\"{}\",\"error\":\"{}\",\"message\":\"{}\"}}",
        json_escape(op),
        json_escape(error),
        json_escape(message)
    );
    1
}

fn fail_closed_not_implemented(op: &str) -> i32 {
    print_json_error(
        op,
        "opaque_backend_not_implemented",
        "OPAQUE Rust helper scaffold only; this protocol operation is not implemented yet",
    )
}

fn create_server_setup(path: &str) -> i32 {
    if path.trim().is_empty() {
        return print_json_error(
            "server-setup-create",
            "opaque_invalid_setup_path",
            "server setup output path is empty",
        );
    }

    let mut rng = OsRng;
    let server_setup = ServerSetup::<PqnasOpaqueCipherSuite>::new(&mut rng);
    let serialized = server_setup.serialize();

    let mut options = OpenOptions::new();
    options.write(true).create_new(true);

    #[cfg(unix)]
    {
        options.mode(0o600);
    }

    let mut file = match options.open(path) {
        Ok(file) => file,
        Err(err) => {
            let message = format!("failed to create server setup file without overwrite: {err}");
            return print_json_error(
                "server-setup-create",
                "opaque_server_setup_create_failed",
                &message,
            );
        }
    };

    if let Err(err) = file.write_all(serialized.as_slice()) {
        let message = format!("failed to write server setup file: {err}");
        return print_json_error(
            "server-setup-create",
            "opaque_server_setup_write_failed",
            &message,
        );
    }

    if let Err(err) = file.sync_all() {
        let message = format!("failed to sync server setup file: {err}");
        return print_json_error(
            "server-setup-create",
            "opaque_server_setup_sync_failed",
            &message,
        );
    }

    println!(
        "{{\"ok\":true,\"op\":\"server-setup-create\",\"path\":\"{}\",\"bytes_written\":{}}}",
        json_escape(path),
        serialized.len()
    );

    0
}

fn print_usage() -> i32 {
    eprintln!(
        "Usage:\n  {PROGRAM_NAME} --version\n  {PROGRAM_NAME} self-test\n  {PROGRAM_NAME} server-setup-create <output-path>\n\nFuture protocol operations are recognized but fail closed:\n  register-start\n  register-finish\n  login-start\n  login-finish"
    );
    2
}

fn main() {
    let mut args = env::args();
    let _program = args.next();

    let Some(arg) = args.next() else {
        process::exit(print_usage());
    };

    let rc = match arg.as_str() {
        "--version" | "version" => {
            if args.next().is_some() {
                print_usage()
            } else {
                print_version()
            }
        }
        "self-test" => {
            if args.next().is_some() {
                print_usage()
            } else {
                run_self_test()
            }
        }
        "server-setup-create" => {
            let Some(path) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                create_server_setup(&path)
            }
        }
        op if is_future_opaque_op(op) => {
            if args.next().is_some() {
                print_usage()
            } else {
                fail_closed_not_implemented(op)
            }
        }
        _ => {
            eprintln!("unknown command: {arg}");
            print_usage()
        }
    };

    process::exit(rc);
}
''')

replace_once(
    "docs/technical/opaque_login_design.md",
    """- Rust helper pins `opaque-ke` as a build dependency, but does not execute production OPAQUE protocol operations yet.
""",
    """- Rust helper pins `opaque-ke` as a build dependency.
- Rust helper implements `server-setup-create <output-path>` for generating a serialized OPAQUE `ServerSetup`; registration and login remain fail-closed.
""",
)

print("done")
