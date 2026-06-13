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
        die(f"anchor not found in {rel}")
    write(rel, text.replace(old, new, 1))

# Cargo dependency for CLI-safe base64 input/output.
replace_once(
    "tools/opaque_helper_rust/Cargo.toml",
    'sha2 = "=0.10.9"\n',
    'sha2 = "=0.10.9"\nbase64 = "=0.22.1"\n',
)

# Imports.
replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use rand::rngs::OsRng;
use opaque_ke::{Ristretto255, ServerSetup, TripleDh};
use sha2::Sha512;
""",
    """use base64::{engine::general_purpose::STANDARD as B64, Engine as _};
use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use opaque_ke::{
    ClientRegistration,
    ClientRegistrationFinishParameters,
    RegistrationRequest,
    RegistrationResponse,
    RegistrationUpload,
    Ristretto255,
    ServerRegistration,
    ServerSetup,
    TripleDh,
};
use rand::rngs::OsRng;
use sha2::Sha512;
""",
)

# register-* are no longer future-fail-closed ops; login-* still are.
replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """fn is_future_opaque_op(op: &str) -> bool {
    matches!(
        op,
        "register-start" | "register-finish" | "login-start" | "login-finish"
    )
}
""",
    """fn is_future_opaque_op(op: &str) -> bool {
    matches!(op, "login-start" | "login-finish")
}
""",
)

# Upgrade self-test to exercise real registration roundtrip in memory.
replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """fn run_self_test() -> i32 {
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
""",
    """fn run_self_test() -> i32 {
    // Security boundary:
    // - OPAQUE ServerSetup generation is available
    // - server-side registration start/finish is available
    // - no login is implemented yet
    // - no users.json access is performed
    // - no PQ-NAS session can be minted by this helper
    let mut rng = OsRng;
    let password = b"pqnas opaque helper self-test password";
    let credential_identifier = b"self-test@example.invalid";

    let server_setup = ServerSetup::<PqnasOpaqueCipherSuite>::new(&mut rng);

    let client_start =
        match ClientRegistration::<PqnasOpaqueCipherSuite>::start(&mut rng, password) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("client registration start failed: {err:?}");
                return print_json_error("self-test", "opaque_self_test_failed", &message);
            }
        };

    let server_start = match ServerRegistration::<PqnasOpaqueCipherSuite>::start(
        &server_setup,
        client_start.message,
        credential_identifier,
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("server registration start failed: {err:?}");
            return print_json_error("self-test", "opaque_self_test_failed", &message);
        }
    };

    let client_finish = match client_start.state.finish(
        &mut rng,
        password,
        server_start.message,
        ClientRegistrationFinishParameters::default(),
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client registration finish failed: {err:?}");
            return print_json_error("self-test", "opaque_self_test_failed", &message);
        }
    };

    let password_file =
        ServerRegistration::<PqnasOpaqueCipherSuite>::finish(client_finish.message);
    let serialized = password_file.serialize();

    if let Err(err) =
        ServerRegistration::<PqnasOpaqueCipherSuite>::deserialize(serialized.as_slice())
    {
        let message = format!("server registration deserialize failed: {err:?}");
        return print_json_error("self-test", "opaque_self_test_failed", &message);
    }

    println!(
        "ok: {PROGRAM_NAME} rust scaffold self-test passed; registration_roundtrip=true; password_file_bytes={}",
        serialized.len()
    );
    0
}
""",
)

# Add helper functions and registration ops after server setup check.
replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """fn print_usage() -> i32 {
""",
    """fn has_control_chars(s: &str) -> bool {
    s.chars().any(|ch| ch.is_control())
}

fn decode_b64_arg(op: &str, label: &str, input: &str, max_len: usize) -> Result<Vec<u8>, i32> {
    let trimmed = input.trim();

    if trimmed.is_empty() {
        let message = format!("{label} is empty");
        return Err(print_json_error(
            op,
            &format!("opaque_{label}_empty"),
            &message,
        ));
    }

    if trimmed.len() > max_len {
        let message = format!("{label} is too large");
        return Err(print_json_error(
            op,
            &format!("opaque_{label}_too_large"),
            &message,
        ));
    }

    B64.decode(trimmed.as_bytes()).map_err(|err| {
        let message = format!("failed to decode {label}: {err}");
        print_json_error(op, &format!("opaque_{label}_invalid"), &message)
    })
}

fn register_start(setup_path: &str, credential_identifier: &str, request_b64: &str) -> i32 {
    const OP: &str = "register-start";

    if setup_path.trim().is_empty() {
        return print_json_error(OP, "opaque_invalid_setup_path", "server setup path is empty");
    }

    if credential_identifier.trim().is_empty() ||
        credential_identifier.len() > 512 ||
        has_control_chars(credential_identifier) {
        return print_json_error(
            OP,
            "opaque_invalid_credential_identifier",
            "credential identifier is empty or invalid",
        );
    }

    let setup_bytes = match fs::read(setup_path) {
        Ok(bytes) => bytes,
        Err(err) => {
            let message = format!("failed to read server setup file: {err}");
            return print_json_error(OP, "opaque_server_setup_read_failed", &message);
        }
    };

    let server_setup: ServerSetup<PqnasOpaqueCipherSuite> =
        match ServerSetup::<PqnasOpaqueCipherSuite>::deserialize(setup_bytes.as_slice()) {
            Ok(setup) => setup,
            Err(err) => {
                let message = format!("failed to deserialize server setup file: {err:?}");
                return print_json_error(OP, "opaque_server_setup_invalid", &message);
            }
        };

    let request_bytes = match decode_b64_arg(OP, "registration_request_b64", request_b64, 8192) {
        Ok(bytes) => bytes,
        Err(rc) => return rc,
    };

    let request =
        match RegistrationRequest::<PqnasOpaqueCipherSuite>::deserialize(request_bytes.as_slice()) {
            Ok(request) => request,
            Err(err) => {
                let message = format!("failed to deserialize registration request: {err:?}");
                return print_json_error(OP, "opaque_registration_request_invalid", &message);
            }
        };

    let result = match ServerRegistration::<PqnasOpaqueCipherSuite>::start(
        &server_setup,
        request,
        credential_identifier.as_bytes(),
    ) {
        Ok(result) => result,
        Err(err) => {
            let message = format!("server registration start failed: {err:?}");
            return print_json_error(OP, "opaque_registration_start_failed", &message);
        }
    };

    let response_bytes = result.message.serialize();
    let response_b64 = B64.encode(response_bytes.as_slice());

    println!(
        r#"{{\"ok\":true,\"op\":\"register-start\",\"registration_response_b64\":\"{}\",\"registration_response_bytes\":{}}}"#,
        json_escape(&response_b64),
        response_bytes.len()
    );

    0
}

fn register_finish(upload_b64: &str) -> i32 {
    const OP: &str = "register-finish";

    let upload_bytes = match decode_b64_arg(OP, "registration_upload_b64", upload_b64, 262144) {
        Ok(bytes) => bytes,
        Err(rc) => return rc,
    };

    let upload =
        match RegistrationUpload::<PqnasOpaqueCipherSuite>::deserialize(upload_bytes.as_slice()) {
            Ok(upload) => upload,
            Err(err) => {
                let message = format!("failed to deserialize registration upload: {err:?}");
                return print_json_error(OP, "opaque_registration_upload_invalid", &message);
            }
        };

    let password_file = ServerRegistration::<PqnasOpaqueCipherSuite>::finish(upload);
    let serialized = password_file.serialize();
    let password_file_b64 = B64.encode(serialized.as_slice());

    println!(
        r#"{{\"ok\":true,\"op\":\"register-finish\",\"opaque_password_file_b64\":\"{}\",\"opaque_password_file_bytes\":{}}}"#,
        json_escape(&password_file_b64),
        serialized.len()
    );

    0
}

fn print_usage() -> i32 {
""",
)

# Usage.
replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """        "Usage:\\n  {PROGRAM_NAME} --version\\n  {PROGRAM_NAME} self-test\\n  {PROGRAM_NAME} server-setup-create <output-path>\\n  {PROGRAM_NAME} server-setup-check <input-path>\\n\\nFuture protocol operations are recognized but fail closed:\\n  register-start\\n  register-finish\\n  login-start\\n  login-finish"
""",
    """        "Usage:\\n  {PROGRAM_NAME} --version\\n  {PROGRAM_NAME} self-test\\n  {PROGRAM_NAME} server-setup-create <output-path>\\n  {PROGRAM_NAME} server-setup-check <input-path>\\n  {PROGRAM_NAME} register-start <server-setup-path> <credential-id> <registration-request-b64>\\n  {PROGRAM_NAME} register-finish <registration-upload-b64>\\n\\nLogin protocol operations are recognized but fail closed:\\n  login-start\\n  login-finish"
""",
)

# Match arms.
replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """        "server-setup-check" => {
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
""",
    """        "server-setup-check" => {
            let Some(path) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                check_server_setup(&path)
            }
        }
        "register-start" => {
            let Some(setup_path) = args.next() else {
                process::exit(print_usage());
            };
            let Some(credential_identifier) = args.next() else {
                process::exit(print_usage());
            };
            let Some(request_b64) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                register_start(&setup_path, &credential_identifier, &request_b64)
            }
        }
        "register-finish" => {
            let Some(upload_b64) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                register_finish(&upload_b64)
            }
        }
        op if is_future_opaque_op(op) => {
""",
)

# Update Rust helper regression test for invalid registration args.
replace_once(
    "tests/opaque_helper_rust/test_opaque_helper_rust.py",
    """        if setup_path.stat().st_size != actual_size:
            fail("server setup file size changed after rejected overwrite attempt")

    login = run([str(helper), "login-start"], expect_rc=1)
""",
    """        if setup_path.stat().st_size != actual_size:
            fail("server setup file size changed after rejected overwrite attempt")

        bad_register_start = run(
            [
                str(helper),
                "register-start",
                str(setup_path),
                "user@example.invalid",
                "not-base64",
            ],
            expect_rc=1,
        )
        bad_register_start_json = parse_json_stdout(
            bad_register_start,
            "bad register-start",
        )
        if bad_register_start_json.get("error") != "opaque_registration_request_b64_invalid":
            fail(f"bad register-start returned wrong error: {bad_register_start_json}")

        bad_register_finish = run(
            [str(helper), "register-finish", "not-base64"],
            expect_rc=1,
        )
        bad_register_finish_json = parse_json_stdout(
            bad_register_finish,
            "bad register-finish",
        )
        if bad_register_finish_json.get("error") != "opaque_registration_upload_b64_invalid":
            fail(f"bad register-finish returned wrong error: {bad_register_finish_json}")

    login = run([str(helper), "login-start"], expect_rc=1)
""",
)

# Documentation.
doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## Admin enrollment storage scaffold\n"
section = """## Rust helper registration operations

The experimental Rust OPAQUE helper now implements the server-side
registration protocol operations:

- `register-start <server-setup-path> <credential-id> <registration-request-b64>`
- `register-finish <registration-upload-b64>`

`register-start` reads and validates the OPAQUE server setup, decodes a
client registration request, and returns `registration_response_b64`.

`register-finish` decodes a client registration upload and returns
`opaque_password_file_b64`, which can later be stored through the
admin-only enrollment storage scaffold.

Security boundary:

- the helper does not read `users.json`
- the helper does not write `opaque_credentials.json`
- the helper does not mint `pqnas_session`
- login protocol operations still fail closed
- `ready_for_login` remains `false`

"""
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    write(doc, text.replace(anchor, section + anchor, 1))
else:
    print(f"unchanged: {doc}")

print("done")
