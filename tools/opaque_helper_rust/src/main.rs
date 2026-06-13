use std::env;
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
