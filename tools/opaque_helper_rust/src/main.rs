use std::env;
use std::fs::{self, OpenOptions};
use std::io::Write;
#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;
use std::process;

use base64::{engine::general_purpose::STANDARD as B64, Engine as _};
use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use opaque_ke::{
    ClientLogin,
    ClientLoginFinishParameters,
    ClientRegistration,
    ClientRegistrationFinishParameters,
    CredentialFinalization,
    CredentialRequest,
    RegistrationRequest,
    RegistrationUpload,
    Ristretto255,
    ServerLogin,
    ServerLoginParameters,
    ServerRegistration,
    ServerSetup,
    TripleDh,
};
use rand::rngs::OsRng;
use sha2::Sha512;

const PROGRAM_NAME: &str = "pqnas_opaque_helper";
const VERSION: &str = "0.1.0-rust-scaffold";

struct PqnasOpaqueCipherSuite;

impl CipherSuite for PqnasOpaqueCipherSuite {
    type OprfCs = Ristretto255;
    type KeyExchange = TripleDh<Ristretto255, Sha512>;
    type Ksf = Argon2<'static>;
}


fn print_version() -> i32 {
    println!("{PROGRAM_NAME} {VERSION}");
    0
}

fn run_self_test() -> i32 {
    // Security boundary:
    // - OPAQUE ServerSetup generation is available
    // - server-side registration start/finish is available
    // - server-side login start/finish is available
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

    let login_password_file =
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

fn has_control_chars(s: &str) -> bool {
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
        r#"{{"ok":true,"op":"register-start","registration_response_b64":"{}","registration_response_bytes":{}}}"#,
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
    eprintln!(
        "Usage:\n  {PROGRAM_NAME} --version\n  {PROGRAM_NAME} self-test\n  {PROGRAM_NAME} server-setup-create <output-path>\n  {PROGRAM_NAME} server-setup-check <input-path>\n  {PROGRAM_NAME} register-start <server-setup-path> <credential-id> <registration-request-b64>\n  {PROGRAM_NAME} register-finish <registration-upload-b64>\n  {PROGRAM_NAME} login-start <server-setup-path> <opaque-password-file-b64> <credential-id> <credential-request-b64>\n  {PROGRAM_NAME} login-finish <server-login-state-b64> <credential-finalization-b64>\n\nLogin helper operations prove the OPAQUE transcript only; this helper never mints PQ-NAS sessions."
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
        _ => {
            eprintln!("unknown command: {arg}");
            print_usage()
        }
    };

    process::exit(rc);
}
