use std::env;
use std::fs::{self, OpenOptions};
use std::io::Write;
#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;
use std::process;

use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use rand::rngs::OsRng;
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

fn check_server_setup(path: &str) -> i32 {
    if path.trim().is_empty() {
        return print_json_error(
            "server-setup-check",
            "opaque_invalid_setup_path",
            "server setup input path is empty",
        );
    }

    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(err) => {
            let message = format!("failed to read server setup file: {err}");
            return print_json_error(
                "server-setup-check",
                "opaque_server_setup_read_failed",
                &message,
            );
        }
    };

    let _setup: ServerSetup<PqnasOpaqueCipherSuite> =
        match ServerSetup::<PqnasOpaqueCipherSuite>::deserialize(bytes.as_slice()) {
            Ok(setup) => setup,
            Err(err) => {
                let message = format!("failed to deserialize server setup file: {err:?}");
                return print_json_error(
                    "server-setup-check",
                    "opaque_server_setup_invalid",
                    &message,
                );
            }
        };

    println!(
        "{{\"ok\":true,\"op\":\"server-setup-check\",\"path\":\"{}\",\"bytes_read\":{}}}",
        json_escape(path),
        bytes.len()
    );

    0
}

fn print_usage() -> i32 {
    eprintln!(
        "Usage:\n  {PROGRAM_NAME} --version\n  {PROGRAM_NAME} self-test\n  {PROGRAM_NAME} server-setup-create <output-path>\n  {PROGRAM_NAME} server-setup-check <input-path>\n\nFuture protocol operations are recognized but fail closed:\n  register-start\n  register-finish\n  login-start\n  login-finish"
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
        "server-setup-check" => {
            let Some(path) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                check_server_setup(&path)
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
