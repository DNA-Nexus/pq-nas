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
    "tools/opaque_helper_rust/src/main.rs",
    "use std::fs::OpenOptions;\n",
    "use std::fs::{self, OpenOptions};\n",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    r'''fn create_server_setup(path: &str) -> i32 {
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
''',
    r'''fn create_server_setup(path: &str) -> i32 {
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
''',
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    '        "Usage:\\n  {PROGRAM_NAME} --version\\n  {PROGRAM_NAME} self-test\\n  {PROGRAM_NAME} server-setup-create <output-path>\\n\\nFuture protocol operations are recognized but fail closed:\\n  register-start\\n  register-finish\\n  login-start\\n  login-finish"\n',
    '        "Usage:\\n  {PROGRAM_NAME} --version\\n  {PROGRAM_NAME} self-test\\n  {PROGRAM_NAME} server-setup-create <output-path>\\n  {PROGRAM_NAME} server-setup-check <input-path>\\n\\nFuture protocol operations are recognized but fail closed:\\n  register-start\\n  register-finish\\n  login-start\\n  login-finish"\n',
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    r'''        "server-setup-create" => {
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
''',
    r'''        "server-setup-create" => {
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
''',
)

replace_once(
    "tests/opaque_helper_rust/test_opaque_helper_rust.py",
    r'''        mode = stat.S_IMODE(setup_path.stat().st_mode)
        if mode != 0o600:
            fail(f"server setup file mode was {oct(mode)}, expected 0o600")

        second = run([str(helper), "server-setup-create", str(setup_path)], expect_rc=1)
''',
    r'''        mode = stat.S_IMODE(setup_path.stat().st_mode)
        if mode != 0o600:
            fail(f"server setup file mode was {oct(mode)}, expected 0o600")

        check = run([str(helper), "server-setup-check", str(setup_path)], expect_rc=0)
        check_json = parse_json_stdout(check, "server-setup-check")
        if check_json.get("ok") is not True:
            fail(f"server-setup-check did not return ok:true: {check_json}")
        if check_json.get("op") != "server-setup-check":
            fail(f"server-setup-check returned wrong op: {check_json}")
        if check_json.get("path") != str(setup_path):
            fail(f"server-setup-check returned wrong path: {check_json}")
        if check_json.get("bytes_read") != actual_size:
            fail(f"server-setup-check bytes_read mismatch: {check_json}")

        invalid_path = Path(tmp) / "invalid_server_setup.bin"
        invalid_path.write_bytes(b"not a valid opaque server setup")
        invalid = run([str(helper), "server-setup-check", str(invalid_path)], expect_rc=1)
        invalid_json = parse_json_stdout(invalid, "invalid server-setup-check")
        if invalid_json.get("error") != "opaque_server_setup_invalid":
            fail(f"invalid server-setup-check returned wrong error: {invalid_json}")

        missing_path = Path(tmp) / "missing_server_setup.bin"
        missing = run([str(helper), "server-setup-check", str(missing_path)], expect_rc=1)
        missing_json = parse_json_stdout(missing, "missing server-setup-check")
        if missing_json.get("error") != "opaque_server_setup_read_failed":
            fail(f"missing server-setup-check returned wrong error: {missing_json}")

        second = run([str(helper), "server-setup-create", str(setup_path)], expect_rc=1)
''',
)

replace_once(
    "docs/technical/opaque_login_design.md",
    "- Rust helper implements `server-setup-create <output-path>` for generating a serialized OPAQUE `ServerSetup`; registration and login remain fail-closed.\n",
    "- Rust helper implements `server-setup-create <output-path>` for generating a serialized OPAQUE `ServerSetup` and `server-setup-check <input-path>` for validating that the file deserializes; registration and login remain fail-closed.\n",
)

print("done")
