use std::env;
use std::process;

use base64::{engine::general_purpose::STANDARD as B64, Engine as _};
use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use opaque_ke::{
    ClientRegistration,
    ClientRegistrationFinishParameters,
    RegistrationResponse,
    Ristretto255,
    TripleDh,
};
use rand::rngs::OsRng;
use sha2::Sha512;

const PASSWORD: &[u8] = b"pqnas opaque client fixture password";

struct PqnasOpaqueCipherSuite;

impl CipherSuite for PqnasOpaqueCipherSuite {
    type OprfCs = Ristretto255;
    type KeyExchange = TripleDh<Ristretto255, Sha512>;
    type Ksf = Argon2<'static>;
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

fn json_error(op: &str, error: &str, message: &str) -> i32 {
    println!(
        "{{\"ok\":false,\"op\":\"{}\",\"error\":\"{}\",\"message\":\"{}\"}}",
        json_escape(op),
        json_escape(error),
        json_escape(message),
    );
    1
}

fn registration_start_fixture() -> i32 {
    let mut rng = OsRng;

    let result =
        match ClientRegistration::<PqnasOpaqueCipherSuite>::start(&mut rng, PASSWORD) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("client registration start failed: {err:?}");
                return json_error("registration-start-fixture", "client_start_failed", &message);
            }
        };

    let state_b64 = B64.encode(result.state.serialize().as_slice());
    let request_b64 = B64.encode(result.message.serialize().as_slice());

    println!(
        "{{\"ok\":true,\"op\":\"registration-start-fixture\",\"client_state_b64\":\"{}\",\"registration_request_b64\":\"{}\"}}",
        json_escape(&state_b64),
        json_escape(&request_b64),
    );

    0
}

fn registration_finish_fixture(state_b64: &str, response_b64: &str) -> i32 {
    let state_bytes = match B64.decode(state_b64.trim().as_bytes()) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client state base64 decode failed: {err}");
            return json_error("registration-finish-fixture", "client_state_invalid", &message);
        }
    };

    let response_bytes = match B64.decode(response_b64.trim().as_bytes()) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("registration response base64 decode failed: {err}");
            return json_error("registration-finish-fixture", "registration_response_invalid", &message);
        }
    };

    let state =
        match ClientRegistration::<PqnasOpaqueCipherSuite>::deserialize(state_bytes.as_slice()) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("client state deserialize failed: {err:?}");
                return json_error("registration-finish-fixture", "client_state_invalid", &message);
            }
        };

    let response =
        match RegistrationResponse::<PqnasOpaqueCipherSuite>::deserialize(response_bytes.as_slice()) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("registration response deserialize failed: {err:?}");
                return json_error("registration-finish-fixture", "registration_response_invalid", &message);
            }
        };

    let mut rng = OsRng;
    let result = match state.finish(
        &mut rng,
        PASSWORD,
        response,
        ClientRegistrationFinishParameters::default(),
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client registration finish failed: {err:?}");
            return json_error("registration-finish-fixture", "client_finish_failed", &message);
        }
    };

    let upload_b64 = B64.encode(result.message.serialize().as_slice());

    println!(
        "{{\"ok\":true,\"op\":\"registration-finish-fixture\",\"registration_upload_b64\":\"{}\"}}",
        json_escape(&upload_b64),
    );

    0
}

fn usage() -> i32 {
    eprintln!(
        "Usage:\n  opaque_client_fixture registration-start-fixture\n  opaque_client_fixture registration-finish-fixture <client-state-b64> <registration-response-b64>"
    );
    2
}

fn main() {
    let mut args = env::args();
    let _program = args.next();

    let Some(op) = args.next() else {
        process::exit(usage());
    };

    let rc = match op.as_str() {
        "registration-start-fixture" => {
            if args.next().is_some() {
                usage()
            } else {
                registration_start_fixture()
            }
        }
        "registration-finish-fixture" => {
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
    };

    process::exit(rc);
}
