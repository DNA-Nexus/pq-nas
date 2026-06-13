use base64::{engine::general_purpose::STANDARD as B64, Engine as _};
use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use opaque_ke::{
    ClientLogin,
    ClientLoginFinishParameters,
    ClientRegistration,
    ClientRegistrationFinishParameters,
    CredentialResponse,
    RegistrationResponse,
    Ristretto255,
    TripleDh,
};
use rand::rngs::OsRng;
use sha2::Sha512;
use wasm_bindgen::prelude::*;

struct PqnasOpaqueCipherSuite;

impl CipherSuite for PqnasOpaqueCipherSuite {
    type OprfCs = Ristretto255;
    type KeyExchange = TripleDh<Ristretto255, Sha512>;
    type Ksf = Argon2<'static>;
}

fn js_err(message: impl Into<String>) -> JsValue {
    JsValue::from_str(&message.into())
}

fn require_password(password: &str) -> Result<&[u8], JsValue> {
    let bytes = password.as_bytes();

    if bytes.is_empty() {
        return Err(js_err("password_empty"));
    }

    if bytes.len() > 1024 {
        return Err(js_err("password_too_long"));
    }

    Ok(bytes)
}

#[wasm_bindgen(js_name = opaqueLoginStart)]
pub fn opaque_login_start(password: &str) -> Result<String, JsValue> {
    let password_bytes = require_password(password)?;

    let mut rng = OsRng;
    let result = ClientLogin::<PqnasOpaqueCipherSuite>::start(&mut rng, password_bytes)
        .map_err(|err| js_err(format!("opaque_login_start_failed: {err:?}")))?;

    let client_login_state_b64 = B64.encode(result.state.serialize().as_slice());
    let credential_request_b64 = B64.encode(result.message.serialize().as_slice());

    Ok(format!(
        "{{\"ok\":true,\"client_login_state_b64\":\"{}\",\"credential_request_b64\":\"{}\"}}",
        client_login_state_b64,
        credential_request_b64
    ))
}

#[wasm_bindgen(js_name = opaqueLoginFinish)]
pub fn opaque_login_finish(
    password: &str,
    client_login_state_b64: &str,
    credential_response_b64: &str,
) -> Result<String, JsValue> {
    let password_bytes = require_password(password)?;

    let state_bytes = B64
        .decode(client_login_state_b64.trim().as_bytes())
        .map_err(|err| js_err(format!("client_login_state_base64_decode_failed: {err}")))?;

    let response_bytes = B64
        .decode(credential_response_b64.trim().as_bytes())
        .map_err(|err| js_err(format!("credential_response_base64_decode_failed: {err}")))?;

    let state = ClientLogin::<PqnasOpaqueCipherSuite>::deserialize(state_bytes.as_slice())
        .map_err(|err| js_err(format!("client_login_state_deserialize_failed: {err:?}")))?;

    let response = CredentialResponse::<PqnasOpaqueCipherSuite>::deserialize(response_bytes.as_slice())
        .map_err(|err| js_err(format!("credential_response_deserialize_failed: {err:?}")))?;

    let mut rng = OsRng;
    let result = state
        .finish(
            &mut rng,
            password_bytes,
            response,
            ClientLoginFinishParameters::default(),
        )
        .map_err(|err| js_err(format!("opaque_login_finish_failed: {err:?}")))?;

    let credential_finalization_b64 = B64.encode(result.message.serialize().as_slice());

    Ok(format!(
        "{{\"ok\":true,\"credential_finalization_b64\":\"{}\",\"client_session_key_bytes\":{}}}",
        credential_finalization_b64,
        result.session_key.as_slice().len()
    ))
}


#[wasm_bindgen(js_name = opaqueRegistrationStart)]
pub fn opaque_registration_start(password: &str) -> Result<String, JsValue> {
    let password_bytes = require_password(password)?;

    let mut rng = OsRng;
    let result = ClientRegistration::<PqnasOpaqueCipherSuite>::start(&mut rng, password_bytes)
        .map_err(|err| js_err(format!("opaque_registration_start_failed: {err:?}")))?;

    let client_registration_state_b64 = B64.encode(result.state.serialize().as_slice());
    let registration_request_b64 = B64.encode(result.message.serialize().as_slice());

    Ok(format!(
        "{{\"ok\":true,\"client_registration_state_b64\":\"{}\",\"registration_request_b64\":\"{}\"}}",
        client_registration_state_b64,
        registration_request_b64
    ))
}

#[wasm_bindgen(js_name = opaqueRegistrationFinish)]
pub fn opaque_registration_finish(
    password: &str,
    client_registration_state_b64: &str,
    registration_response_b64: &str,
) -> Result<String, JsValue> {
    let password_bytes = require_password(password)?;

    let state_bytes = B64
        .decode(client_registration_state_b64.trim().as_bytes())
        .map_err(|err| js_err(format!("client_registration_state_base64_decode_failed: {err}")))?;

    let response_bytes = B64
        .decode(registration_response_b64.trim().as_bytes())
        .map_err(|err| js_err(format!("registration_response_base64_decode_failed: {err}")))?;

    let state = ClientRegistration::<PqnasOpaqueCipherSuite>::deserialize(state_bytes.as_slice())
        .map_err(|err| js_err(format!("client_registration_state_deserialize_failed: {err:?}")))?;

    let response = RegistrationResponse::<PqnasOpaqueCipherSuite>::deserialize(response_bytes.as_slice())
        .map_err(|err| js_err(format!("registration_response_deserialize_failed: {err:?}")))?;

    let mut rng = OsRng;
    let result = state
        .finish(
            &mut rng,
            password_bytes,
            response,
            ClientRegistrationFinishParameters::default(),
        )
        .map_err(|err| js_err(format!("opaque_registration_finish_failed: {err:?}")))?;

    let registration_upload_b64 = B64.encode(result.message.serialize().as_slice());

    Ok(format!(
        "{{\"ok\":true,\"registration_upload_b64\":\"{}\"}}",
        registration_upload_b64
    ))
}
