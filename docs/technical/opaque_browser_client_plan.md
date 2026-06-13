# OPAQUE Browser Client Plan

Status: design plan  
Scope: browser-side OPAQUE login for DNA-Nexus / PQ-NAS  
Current backend status: server-side OPAQUE registration/enrollment, helper login transcript handling, and `pqnas_session` minting paths exist. Production browser OPAQUE login is not enabled until a compatible browser-side OPAQUE client is implemented and tested.

## Goal

Implement a browser-side OPAQUE login client so users can sign in without sending a plaintext password to the server.

The browser must perform the OPAQUE client-side cryptographic steps locally, then use the existing server endpoints:

- GET /api/auth/config
- POST /api/auth/opaque/login/start
- POST /api/auth/opaque/login/finish

The server must only mint pqnas_session after a valid OPAQUE transcript and an enabled user check.

## Non-goals

This step does not reintroduce classic password login as an OPAQUE fallback.

The browser UI must never send password, plaintext_password, password_hash, classic_password_hash, or argon2id_hash to any OPAQUE endpoint.

If the browser OPAQUE module is missing, incompatible, or fails to load, the UI must show a safe unavailable state instead of falling back to password submission.

## Current safe UI state

The login page detects mode=opaque from /api/auth/config.

In OPAQUE mode, it shows a safe placeholder explaining that browser-side OPAQUE crypto is not installed yet.

This is intentional until a real browser client is added.

## Required browser-side operations

1. Create login start request from user password.
2. Store client login state in memory only.
3. Send credential_request_b64 to /api/auth/opaque/login/start.
4. Receive credential_response_b64 and opaque_login_id.
5. Finalize the OPAQUE transcript locally.
6. Send credential_finalization_b64 to /api/auth/opaque/login/finish.
7. Receive standard session cookie through Set-Cookie: pqnas_session=...
8. Redirect to /app only after /api/v4/me confirms the session cookie works.

## Secrets and memory handling

The plaintext password exists only inside the browser during the login operation. The OPAQUE finish step also needs the password, so the UI may keep it in memory for one login attempt only.

The UI must clear the password input as soon as possible after finalization or failure.

The following values must not be written to localStorage, sessionStorage, IndexedDB, URL params, logs, or DOM attributes:

- plaintext password
- client login state
- session key
- credential finalization material

Allowed to exist temporarily in memory:

- credential_request_b64
- credential_response_b64
- credential_finalization_b64
- opaque_login_id
- client login state object or buffer

The client login state must be scoped to one login attempt and discarded after success or failure.

## Client flow hardening

The browser must allow only one active OPAQUE login attempt at a time.

Rules:

- disable the submit button while an OPAQUE login attempt is active
- discard any previous client login state before starting a new attempt
- never reuse `client_login_state_b64`
- never reuse `opaque_login_id`
- clear the password input after success or failure
- clear in-memory client state after success, failure, timeout, or page navigation
- use `fetch(..., { credentials: "same-origin", cache: "no-store" })`
- redirect to `/app` only after `/api/v4/me` confirms the session cookie works

JavaScript memory clearing is best-effort. The hard security rule is that password material and client state must never be written to storage, logs, URL parameters, or DOM attributes.

All OPAQUE failures shown to the user should use a generic login-failed message.

## Implementation options

### Option A: Rust/WASM client from our existing helper code

Pros:

- Same Rust ecosystem as the server helper.
- Easier to keep OPAQUE suite compatibility aligned.
- We can reuse test vectors and fixture logic.
- Lower risk of subtle suite mismatch.

Cons:

- Requires WASM build pipeline.
- Requires browser-safe API wrapper.
- Bundle size and loading need to be managed.

### Option B: External browser OPAQUE library

Pros:

- Potentially faster UI implementation.
- May avoid maintaining WASM glue ourselves.

Cons:

- Must verify protocol suite compatibility.
- Must verify maintenance status and security quality.
- Must avoid supply-chain risk.
- Browser behavior and serialization may not match our Rust helper.

### Option C: Keep OPAQUE backend-ready only

Pros:

- Safest until browser client is fully reviewed.
- No risk of half-working crypto in production UI.

Cons:

- OPAQUE mode remains unusable for normal browser login.

## Preferred path

Preferred initial implementation: Option A.

Build a small WASM client from Rust, with a narrow JavaScript API.

Required exported functions:

| Function | Input | Output |
| --- | --- | --- |
| opaqueLoginStart | user password | client_login_state_b64 and credential_request_b64 |
| opaqueLoginFinish | password, client_login_state_b64 and credential_response_b64 | credential_finalization_b64 |

The API should not expose unrelated helper operations.

## Required protocol compatibility decisions

Before enabling browser OPAQUE login, the browser client and server helper must explicitly agree on:

- exact OPAQUE library family and version
- exact OPAQUE ciphersuite
- exact serialized message formats
- exact base64 variant
- exact credential identifier format
- exact login normalization rule
- exact server setup format
- exact stored credential format
- exact test vector set used by both browser and server helper

The browser client must fail closed if the server reports an unsupported suite, helper version, or protocol format.

No production OPAQUE mode is allowed until a browser-generated login transcript has been tested end-to-end against the same helper/version/suite used by the server.

## Browser module loading policy

The OPAQUE browser module must be served as a local versioned static asset.

Rules:

- do not load OPAQUE crypto from a CDN
- do not dynamically select an unpinned external package at runtime
- fail closed if the WASM module or JS wrapper is missing
- fail closed if the browser module version is incompatible with the server-supported OPAQUE suite
- show a safe unavailable state instead of falling back to password login
- never call `/api/auth/password/login` while `mode=opaque`

The UI should treat module initialization failure as an authentication-method-unavailable state, not as a password-login opportunity.

## UI requirements

The login UI must use existing shared theme tokens and classes.

Rules:

1. Do not add hardcoded colors.
2. Do not invent new CSS tokens unless they are added to the shared theme intentionally.
3. Prefer existing card, button, input, badge, hint, and status styles.
4. All visible text must use i18n keys.
5. All new i18n keys must be added to every supported file in server/src/static/i18n/*.json.
6. No password fallback is allowed in OPAQUE mode.

## Endpoint flow

### Step 1: config

Request:

GET /api/auth/config

Expected OPAQUE mode response:

- ok: true
- mode: opaque
- opaque_enabled: true
- password_enabled: false
- password_scheme: opaque

### Step 2: login start

Browser computes credential_request_b64.

Request body:

- login
- credential_request_b64

Expected response:

- ok: true
- login
- opaque_login_id
- credential_response_b64
- ready_for_session: false
- session_minting: false

This endpoint must not set pqnas_session.

### Step 3: login finish

Browser computes credential_finalization_b64.

Request body:

- opaque_login_id
- credential_finalization_b64

Expected response:

- ok: true
- authenticated: true
- fingerprint
- role
- ready_for_session: true
- session_minting: true

Expected header:

Set-Cookie: pqnas_session=...

## Failure behavior

All credential failures should remain generic:

- ok: false
- error: invalid_login_or_password

The UI should not distinguish between:

- unknown login
- disabled user
- missing OPAQUE credential
- invalid password
- invalid transcript

This avoids account enumeration.

This rule applies to both `login/start` and `login/finish`. When practical, `login/start` should use a dummy/missing-user flow or otherwise preserve a response shape that does not reveal whether the login exists.

## Required tests before enabling production OPAQUE browser login

### Browser/WASM unit tests

- login start creates valid credential_request_b64
- login finish creates valid credential_finalization_b64
- bad server response fails safely
- client state is one-time use
- password input is cleared after failure or success

### Runtime tests

- enabled OPAQUE user gets pqnas_session
- disabled OPAQUE user does not get pqnas_session
- missing login returns generic failure
- wrong password returns generic failure
- replaying opaque_login_id fails
- OPAQUE mode never calls /api/auth/password/login

### UI checks

- password fallback is not visible in OPAQUE mode
- missing WASM module shows safe unavailable state
- all visible strings are translated
- no new hardcoded colors are added
- only approved CSS tokens and shared classes are used

## Rollout plan

1. Keep password mode as default.
2. Add browser OPAQUE client behind OPAQUE mode only.
3. Test on dev with PQNAS_LOGIN_MODE=opaque.
4. Verify enabled-user login and disabled-user denial.
5. Keep password mode available only when server config explicitly chooses password.
6. Document recovery and admin enrollment flow before production use.

## Open questions

- Should the browser OPAQUE module be built as a separate WASM artifact or bundled with auth_login.js?
- Should OPAQUE registration for normal users happen in browser later, or remain admin-controlled initially?
- Should mobile trusted-device approval become a second factor after OPAQUE login?
- What is the long-term password recovery story for OPAQUE-only accounts?
