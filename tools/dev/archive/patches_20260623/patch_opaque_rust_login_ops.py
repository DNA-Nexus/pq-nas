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

# -------------------------
# tools/opaque_helper_rust/src/main.rs
# -------------------------

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """use opaque_ke::{
    ClientRegistration,
    ClientRegistrationFinishParameters,
    RegistrationRequest,
    RegistrationUpload,
    Ristretto255,
    ServerRegistration,
    ServerSetup,
    TripleDh,
};
""",
    """use opaque_ke::{
    ClientLogin,
    ClientLoginFinishParameters,
    ClientRegistration,
    ClientRegistrationFinishParameters,
    CredentialFinalization,
    CredentialRequest,
    CredentialResponse,
    RegistrationRequest,
    RegistrationUpload,
    Ristretto255,
    ServerLogin,
    ServerLoginParameters,
    ServerRegistration,
    ServerSetup,
    TripleDh,
};
""",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """fn is_future_opaque_op(op: &str) -> bool {
    matches!(op, "login-start" | "login-finish")
}

""",
    "",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """    // Security boundary:
    // - OPAQUE ServerSetup generation is available
    // - server-side registration start/finish is available
    // - no login is implemented yet
    // - no users.json access is performed
    // - no PQ-NAS session can be minted by this helper
""",
    """    // Security boundary:
    // - OPAQUE ServerSetup generation is available
    // - server-side registration start/finish is available
    // - server-side login start/finish is available
    // - no users.json access is performed
    // - no PQ-NAS session can be minted by this helper
""",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """    if let Err(err) =
        ServerRegistration::<PqnasOpaqueCipherSuite>::deserialize(serialized.as_slice())
    {
        let message = format!("server registration deserialize failed: {err:?}");
        return print_json_error("self-test", "opaque_self_test_failed", &message);
    }

    println!(
        "ok: {PROGRAM_NAME} rust scaffold self-test passed; registration_roundtrip=true; password_file_bytes={}",
        serialized.len()
    );
""",
    """    let login_password_file =
        match ServerRegistration::<PqnasOpaqueCipherSuite>::deserialize(serialized.as_slice()) {
            Ok(password_file) => password_file,
            Err(err) => {
                let message = format!("server registration deserialize failed: {err:?}");
                return print_json_error("self-test", "opaque_self_test_failed", &message);
            }
        };

    let client_login_start =
        match ClientLogin::<PqnasOpaqueCipherSuite>::start(&mut rng, password) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("client login start failed: {err:?}");
                return print_json_error("self-test", "opaque_self_test_failed", &message);
            }
        };

    let server_login_start = match ServerLogin::<PqnasOpaqueCipherSuite>::start(
        &mut rng,
        &server_setup,
        Some(login_password_file),
        client_login_start.message,
        credential_identifier,
        ServerLoginParameters::default(),
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("server login start failed: {err:?}");
            return print_json_error("self-test", "opaque_self_test_failed", &message);
        }
    };

    let client_login_finish = match client_login_start.state.finish(
        &mut rng,
        password,
        server_login_start.message,
        ClientLoginFinishParameters::default(),
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client login finish failed: {err:?}");
            return print_json_error("self-test", "opaque_self_test_failed", &message);
        }
    };

    let server_login_finish = match server_login_start.state.finish(
        client_login_finish.message,
        ServerLoginParameters::default(),
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("server login finish failed: {err:?}");
            return print_json_error("self-test", "opaque_self_test_failed", &message);
        }
    };

    if client_login_finish.session_key != server_login_finish.session_key {
        return print_json_error(
            "self-test",
            "opaque_self_test_failed",
            "client/server login session keys did not match",
        );
    }

    println!(
        "ok: {PROGRAM_NAME} rust scaffold self-test passed; registration_roundtrip=true; login_roundtrip=true; password_file_bytes={}",
        serialized.len()
    );
""",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """fn register_finish(upload_b64: &str) -> i32 {
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
        r#"{{"ok":true,"op":"register-finish","opaque_password_file_b64":"{}","opaque_password_file_bytes":{}}}"#,
        json_escape(&password_file_b64),
        serialized.len()
    );

    0
}

fn print_usage() -> i32 {
""",
    """fn register_finish(upload_b64: &str) -> i32 {
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
        r#"{{"ok":true,"op":"register-finish","opaque_password_file_b64":"{}","opaque_password_file_bytes":{}}}"#,
        json_escape(&password_file_b64),
        serialized.len()
    );

    0
}

fn login_start(
    setup_path: &str,
    password_file_b64: &str,
    credential_identifier: &str,
    credential_request_b64: &str,
) -> i32 {
    const OP: &str = "login-start";

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

    let password_file_bytes =
        match decode_b64_arg(OP, "opaque_password_file_b64", password_file_b64, 262144) {
            Ok(bytes) => bytes,
            Err(rc) => return rc,
        };

    let password_file =
        match ServerRegistration::<PqnasOpaqueCipherSuite>::deserialize(password_file_bytes.as_slice()) {
            Ok(password_file) => password_file,
            Err(err) => {
                let message = format!("failed to deserialize OPAQUE password file: {err:?}");
                return print_json_error(OP, "opaque_password_file_invalid", &message);
            }
        };

    let credential_request_bytes =
        match decode_b64_arg(OP, "credential_request_b64", credential_request_b64, 8192) {
            Ok(bytes) => bytes,
            Err(rc) => return rc,
        };

    let credential_request =
        match CredentialRequest::<PqnasOpaqueCipherSuite>::deserialize(credential_request_bytes.as_slice()) {
            Ok(request) => request,
            Err(err) => {
                let message = format!("failed to deserialize credential request: {err:?}");
                return print_json_error(OP, "opaque_credential_request_invalid", &message);
            }
        };

    let mut rng = OsRng;
    let result = match ServerLogin::<PqnasOpaqueCipherSuite>::start(
        &mut rng,
        &server_setup,
        Some(password_file),
        credential_request,
        credential_identifier.as_bytes(),
        ServerLoginParameters::default(),
    ) {
        Ok(result) => result,
        Err(err) => {
            let message = format!("server login start failed: {err:?}");
            return print_json_error(OP, "opaque_login_start_failed", &message);
        }
    };

    let credential_response_bytes = result.message.serialize();
    let server_login_state_bytes = result.state.serialize();

    let credential_response_b64 = B64.encode(credential_response_bytes.as_slice());
    let server_login_state_b64 = B64.encode(server_login_state_bytes.as_slice());

    println!(
        r#"{{"ok":true,"op":"login-start","credential_response_b64":"{}","credential_response_bytes":{},"server_login_state_b64":"{}","server_login_state_bytes":{}}}"#,
        json_escape(&credential_response_b64),
        credential_response_bytes.len(),
        json_escape(&server_login_state_b64),
        server_login_state_bytes.len()
    );

    0
}

fn login_finish(server_login_state_b64: &str, credential_finalization_b64: &str) -> i32 {
    const OP: &str = "login-finish";

    let server_login_state_bytes =
        match decode_b64_arg(OP, "server_login_state_b64", server_login_state_b64, 16384) {
            Ok(bytes) => bytes,
            Err(rc) => return rc,
        };

    let server_login =
        match ServerLogin::<PqnasOpaqueCipherSuite>::deserialize(server_login_state_bytes.as_slice()) {
            Ok(state) => state,
            Err(err) => {
                let message = format!("failed to deserialize server login state: {err:?}");
                return print_json_error(OP, "opaque_server_login_state_invalid", &message);
            }
        };

    let finalization_bytes =
        match decode_b64_arg(OP, "credential_finalization_b64", credential_finalization_b64, 8192) {
            Ok(bytes) => bytes,
            Err(rc) => return rc,
        };

    let finalization =
        match CredentialFinalization::<PqnasOpaqueCipherSuite>::deserialize(finalization_bytes.as_slice()) {
            Ok(finalization) => finalization,
            Err(err) => {
                let message = format!("failed to deserialize credential finalization: {err:?}");
                return print_json_error(OP, "opaque_credential_finalization_invalid", &message);
            }
        };

    let finish_result = match server_login.finish(finalization, ServerLoginParameters::default()) {
        Ok(result) => result,
        Err(err) => {
            let message = format!("server login finish failed: {err:?}");
            return print_json_error(OP, "opaque_login_finish_failed", &message);
        }
    };

    println!(
        r#"{{"ok":true,"op":"login-finish","authenticated":true,"server_session_key_bytes":{}}}"#,
        finish_result.session_key.as_slice().len()
    );

    0
}

fn print_usage() -> i32 {
""",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """        "Usage:\\n  {PROGRAM_NAME} --version\\n  {PROGRAM_NAME} self-test\\n  {PROGRAM_NAME} server-setup-create <output-path>\\n  {PROGRAM_NAME} server-setup-check <input-path>\\n  {PROGRAM_NAME} register-start <server-setup-path> <credential-id> <registration-request-b64>\\n  {PROGRAM_NAME} register-finish <registration-upload-b64>\\n\\nLogin protocol operations are recognized but fail closed:\\n  login-start\\n  login-finish"
""",
    """        "Usage:\\n  {PROGRAM_NAME} --version\\n  {PROGRAM_NAME} self-test\\n  {PROGRAM_NAME} server-setup-create <output-path>\\n  {PROGRAM_NAME} server-setup-check <input-path>\\n  {PROGRAM_NAME} register-start <server-setup-path> <credential-id> <registration-request-b64>\\n  {PROGRAM_NAME} register-finish <registration-upload-b64>\\n  {PROGRAM_NAME} login-start <server-setup-path> <opaque-password-file-b64> <credential-id> <credential-request-b64>\\n  {PROGRAM_NAME} login-finish <server-login-state-b64> <credential-finalization-b64>\\n\\nLogin helper operations prove the OPAQUE transcript only; this helper never mints PQ-NAS sessions."
""",
)

replace_once(
    "tools/opaque_helper_rust/src/main.rs",
    """        "register-finish" => {
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
            if args.next().is_some() {
                print_usage()
            } else {
                fail_closed_not_implemented(op)
            }
        }
""",
    """        "register-finish" => {
            let Some(upload_b64) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                register_finish(&upload_b64)
            }
        }
        "login-start" => {
            let Some(setup_path) = args.next() else {
                process::exit(print_usage());
            };
            let Some(password_file_b64) = args.next() else {
                process::exit(print_usage());
            };
            let Some(credential_identifier) = args.next() else {
                process::exit(print_usage());
            };
            let Some(credential_request_b64) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                login_start(
                    &setup_path,
                    &password_file_b64,
                    &credential_identifier,
                    &credential_request_b64,
                )
            }
        }
        "login-finish" => {
            let Some(server_login_state_b64) = args.next() else {
                process::exit(print_usage());
            };
            let Some(credential_finalization_b64) = args.next() else {
                process::exit(print_usage());
            };

            if args.next().is_some() {
                print_usage()
            } else {
                login_finish(&server_login_state_b64, &credential_finalization_b64)
            }
        }
""",
)

# -------------------------
# tools/opaque_helper_rust/src/bin/opaque_client_fixture.rs
# -------------------------

replace_once(
    "tools/opaque_helper_rust/src/bin/opaque_client_fixture.rs",
    """use opaque_ke::{
    ClientRegistration,
    ClientRegistrationFinishParameters,
    RegistrationResponse,
    Ristretto255,
    TripleDh,
};
""",
    """use opaque_ke::{
    ClientLogin,
    ClientLoginFinishParameters,
    ClientRegistration,
    ClientRegistrationFinishParameters,
    CredentialResponse,
    RegistrationResponse,
    Ristretto255,
    TripleDh,
};
""",
)

replace_once(
    "tools/opaque_helper_rust/src/bin/opaque_client_fixture.rs",
    """fn registration_finish_fixture(state_b64: &str, response_b64: &str) -> i32 {
""",
    """fn login_start_fixture() -> i32 {
    let mut rng = OsRng;

    let result = match ClientLogin::<PqnasOpaqueCipherSuite>::start(&mut rng, PASSWORD) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client login start failed: {err:?}");
            return json_error("login-start-fixture", "client_login_start_failed", &message);
        }
    };

    let state_b64 = B64.encode(result.state.serialize().as_slice());
    let credential_request_b64 = B64.encode(result.message.serialize().as_slice());

    println!(
        "{{\"ok\":true,\"op\":\"login-start-fixture\",\"client_login_state_b64\":\"{}\",\"credential_request_b64\":\"{}\"}}",
        json_escape(&state_b64),
        json_escape(&credential_request_b64),
    );

    0
}

fn login_finish_fixture(state_b64: &str, credential_response_b64: &str) -> i32 {
    let state_bytes = match B64.decode(state_b64.trim().as_bytes()) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client login state base64 decode failed: {err}");
            return json_error("login-finish-fixture", "client_login_state_invalid", &message);
        }
    };

    let response_bytes = match B64.decode(credential_response_b64.trim().as_bytes()) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("credential response base64 decode failed: {err}");
            return json_error("login-finish-fixture", "credential_response_invalid", &message);
        }
    };

    let state = match ClientLogin::<PqnasOpaqueCipherSuite>::deserialize(state_bytes.as_slice()) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client login state deserialize failed: {err:?}");
            return json_error("login-finish-fixture", "client_login_state_invalid", &message);
        }
    };

    let response =
        match CredentialResponse::<PqnasOpaqueCipherSuite>::deserialize(response_bytes.as_slice()) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("credential response deserialize failed: {err:?}");
                return json_error("login-finish-fixture", "credential_response_invalid", &message);
            }
        };

    let mut rng = OsRng;
    let result = match state.finish(
        &mut rng,
        PASSWORD,
        response,
        ClientLoginFinishParameters::default(),
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client login finish failed: {err:?}");
            return json_error("login-finish-fixture", "client_login_finish_failed", &message);
        }
    };

    let finalization_b64 = B64.encode(result.message.serialize().as_slice());

    println!(
        "{{\"ok\":true,\"op\":\"login-finish-fixture\",\"credential_finalization_b64\":\"{}\",\"client_session_key_bytes\":{}}}",
        json_escape(&finalization_b64),
        result.session_key.as_slice().len(),
    );

    0
}

fn registration_finish_fixture(state_b64: &str, response_b64: &str) -> i32 {
""",
)

replace_once(
    "tools/opaque_helper_rust/src/bin/opaque_client_fixture.rs",
    """        "Usage:\\n  opaque_client_fixture registration-start-fixture\\n  opaque_client_fixture registration-finish-fixture <client-state-b64> <registration-response-b64>"
""",
    """        "Usage:\\n  opaque_client_fixture registration-start-fixture\\n  opaque_client_fixture registration-finish-fixture <client-state-b64> <registration-response-b64>\\n  opaque_client_fixture login-start-fixture\\n  opaque_client_fixture login-finish-fixture <client-login-state-b64> <credential-response-b64>"
""",
)

replace_once(
    "tools/opaque_helper_rust/src/bin/opaque_client_fixture.rs",
    """        "registration-finish-fixture" => {
            let Some(state_b64) = args.next() else {
                process::exit(usage());
            };
            let Some(response_b64) = args.next() else {
                process::exit(usage());
            };

            if args.next().is_some() {
                usage()
            } else {
                registration_finish_fixture(&state_b64, &response_b64)
            }
        }
        _ => usage(),
""",
    """        "registration-finish-fixture" => {
            let Some(state_b64) = args.next() else {
                process::exit(usage());
            };
            let Some(response_b64) = args.next() else {
                process::exit(usage());
            };

            if args.next().is_some() {
                usage()
            } else {
                registration_finish_fixture(&state_b64, &response_b64)
            }
        }
        "login-start-fixture" => {
            if args.next().is_some() {
                usage()
            } else {
                login_start_fixture()
            }
        }
        "login-finish-fixture" => {
            let Some(state_b64) = args.next() else {
                process::exit(usage());
            };
            let Some(response_b64) = args.next() else {
                process::exit(usage());
            };

            if args.next().is_some() {
                usage()
            } else {
                login_finish_fixture(&state_b64, &response_b64)
            }
        }
        _ => usage(),
""",
)

# -------------------------
# tests/opaque_helper_rust/test_opaque_helper_rust.py
# -------------------------

replace_once(
    "tests/opaque_helper_rust/test_opaque_helper_rust.py",
    """def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: test_opaque_helper_rust.py <pqnas_opaque_helper_rust_binary>")

    helper = Path(sys.argv[1])
""",
    """def cargo_fixture(*fixture_args):
    repo_root = Path(__file__).resolve().parents[2]
    manifest = repo_root / "tools" / "opaque_helper_rust" / "Cargo.toml"

    result = run(
        [
            "cargo",
            "run",
            "--quiet",
            "--manifest-path",
            str(manifest),
            "--bin",
            "opaque_client_fixture",
            "--",
            *fixture_args,
        ],
        expect_rc=0,
    )
    return parse_json_stdout(result, "opaque_client_fixture " + " ".join(fixture_args[:1]))


def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: test_opaque_helper_rust.py <pqnas_opaque_helper_rust_binary>")

    helper = Path(sys.argv[1])
""",
)

replace_once(
    "tests/opaque_helper_rust/test_opaque_helper_rust.py",
    """        bad_register_finish = run(
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
    login_json = parse_json_stdout(login, "login-start")
    if login_json.get("ok") is not False:
        fail(f"login-start should fail closed: {login_json}")
    if login_json.get("error") != "opaque_backend_not_implemented":
        fail(f"login-start returned wrong error: {login_json}")

    print("ok: Rust OPAQUE helper regression tests passed")
""",
    """        bad_register_finish = run(
            [str(helper), "register-finish", "not-base64"],
            expect_rc=1,
        )
        bad_register_finish_json = parse_json_stdout(
            bad_register_finish,
            "bad register-finish",
        )
        if bad_register_finish_json.get("error") != "opaque_registration_upload_b64_invalid":
            fail(f"bad register-finish returned wrong error: {bad_register_finish_json}")

        client_reg_start = cargo_fixture("registration-start-fixture")
        client_reg_state_b64 = client_reg_start.get("client_state_b64", "")
        registration_request_b64 = client_reg_start.get("registration_request_b64", "")
        if not client_reg_state_b64 or not registration_request_b64:
            fail(f"registration fixture missing fields: {client_reg_start}")

        server_reg_start = run(
            [
                str(helper),
                "register-start",
                str(setup_path),
                "user@example.invalid",
                registration_request_b64,
            ],
            expect_rc=0,
        )
        server_reg_start_json = parse_json_stdout(server_reg_start, "register-start roundtrip")
        registration_response_b64 = server_reg_start_json.get("registration_response_b64", "")
        if not registration_response_b64:
            fail(f"register-start missing registration_response_b64: {server_reg_start_json}")

        client_reg_finish = cargo_fixture(
            "registration-finish-fixture",
            client_reg_state_b64,
            registration_response_b64,
        )
        registration_upload_b64 = client_reg_finish.get("registration_upload_b64", "")
        if not registration_upload_b64:
            fail(f"registration finish fixture missing upload: {client_reg_finish}")

        server_reg_finish = run(
            [str(helper), "register-finish", registration_upload_b64],
            expect_rc=0,
        )
        server_reg_finish_json = parse_json_stdout(server_reg_finish, "register-finish roundtrip")
        opaque_password_file_b64 = server_reg_finish_json.get("opaque_password_file_b64", "")
        if not opaque_password_file_b64:
            fail(f"register-finish missing password file: {server_reg_finish_json}")

        client_login_start = cargo_fixture("login-start-fixture")
        client_login_state_b64 = client_login_start.get("client_login_state_b64", "")
        credential_request_b64 = client_login_start.get("credential_request_b64", "")
        if not client_login_state_b64 or not credential_request_b64:
            fail(f"login start fixture missing fields: {client_login_start}")

        server_login_start = run(
            [
                str(helper),
                "login-start",
                str(setup_path),
                opaque_password_file_b64,
                "user@example.invalid",
                credential_request_b64,
            ],
            expect_rc=0,
        )
        server_login_start_json = parse_json_stdout(server_login_start, "login-start roundtrip")
        credential_response_b64 = server_login_start_json.get("credential_response_b64", "")
        server_login_state_b64 = server_login_start_json.get("server_login_state_b64", "")
        if not credential_response_b64 or not server_login_state_b64:
            fail(f"login-start missing expected fields: {server_login_start_json}")

        client_login_finish = cargo_fixture(
            "login-finish-fixture",
            client_login_state_b64,
            credential_response_b64,
        )
        credential_finalization_b64 = client_login_finish.get("credential_finalization_b64", "")
        if not credential_finalization_b64:
            fail(f"login finish fixture missing finalization: {client_login_finish}")

        server_login_finish = run(
            [
                str(helper),
                "login-finish",
                server_login_state_b64,
                credential_finalization_b64,
            ],
            expect_rc=0,
        )
        server_login_finish_json = parse_json_stdout(server_login_finish, "login-finish roundtrip")
        if server_login_finish_json.get("authenticated") is not True:
            fail(f"login-finish did not authenticate: {server_login_finish_json}")

        bad_login_start = run(
            [
                str(helper),
                "login-start",
                str(setup_path),
                opaque_password_file_b64,
                "user@example.invalid",
                "not-base64",
            ],
            expect_rc=1,
        )
        bad_login_start_json = parse_json_stdout(bad_login_start, "bad login-start")
        if bad_login_start_json.get("error") != "opaque_credential_request_b64_invalid":
            fail(f"bad login-start returned wrong error: {bad_login_start_json}")

        bad_login_finish = run(
            [str(helper), "login-finish", "not-base64", "QUJD"],
            expect_rc=1,
        )
        bad_login_finish_json = parse_json_stdout(bad_login_finish, "bad login-finish")
        if bad_login_finish_json.get("error") != "opaque_server_login_state_b64_invalid":
            fail(f"bad login-finish returned wrong error: {bad_login_finish_json}")

    print("ok: Rust OPAQUE helper regression tests passed")
""",
)

# -------------------------
# docs
# -------------------------

doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## Rust helper registration operations\n"
section = """## Rust helper login operations

The Rust helper now implements server-side OPAQUE login operations:

- `login-start <server-setup-path> <opaque-password-file-b64> <credential-id> <credential-request-b64>`
- `login-finish <server-login-state-b64> <credential-finalization-b64>`

`login-start` deserializes the server setup, the stored OPAQUE password file,
and the client credential request. It returns a credential response plus an
opaque serialized server login state.

`login-finish` deserializes the server login state and the client's credential
finalization. It returns `authenticated: true` only if the OPAQUE transcript is
valid.

Security boundary:

- the helper never reads users.json
- the helper never reads or writes opaque_credentials.json
- the helper never mints `pqnas_session`
- public OPAQUE HTTP login remains disabled until reviewed route integration
- successful helper login proves only the OPAQUE transcript

"""
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    write(doc, text.replace(anchor, section + anchor, 1))
else:
    print(f"unchanged: {doc}")

print("done")
