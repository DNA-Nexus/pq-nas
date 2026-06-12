# OPAQUE Login Design for DNA-Nexus / PQ-NAS

## Status

This document is a design plan.

Current implementation status:

- `PQNAS_LOGIN_MODE=opaque` is recognized as a third browser login mode.
- `/api/auth/config` can report OPAQUE mode.
- `/api/auth/opaque/login/start` and `/api/auth/opaque/login/finish` exist as fail-closed scaffold endpoints.
- The login UI shows an OPAQUE-not-configured message instead of silently falling back to classic password login.
- Existing QR login, classic password login, mobile pairing, and app token logic are intentionally unchanged.

The current OPAQUE scaffold must not be considered a working OPAQUE login implementation.

## Core rule

OPAQUE is a new browser-login method.

OPAQUE is not a new user system.

All successful browser login methods must converge to the same existing internal model:

```text
login method -> fingerprint -> users.json enabled/status/role check -> pqnas_session
```

This rule keeps the rest of DNA-Nexus / PQ-NAS independent from the selected login method.

These subsystems must not need to know whether the user logged in with QR, classic password, or OPAQUE:

- File Manager
- admin routes
- user routes
- quotas
- storage pools
- workspaces
- bundled apps
- Circle Stack
- Echo Stack
- Reel Stack
- mobile trusted-device/app-token authorization

## Goals

OPAQUE login should provide a password-based browser login where:

- the browser never sends the plaintext password to the server
- the server does not store a classic password hash for OPAQUE users
- server compromise should not immediately allow offline password guessing against a simple password hash
- successful authentication still resolves to the existing DNA-Nexus fingerprint
- the session cookie remains the existing `pqnas_session`
- disabled users remain blocked even if OPAQUE authentication succeeds cryptographically
- the implementation uses a reviewed OPAQUE library instead of custom cryptography

## Non-goals for the first real OPAQUE implementation

The first real OPAQUE implementation should not try to solve all encryption and recovery problems at once.

Non-goals for phase 1:

- no client-side file encryption redesign
- no encrypted vault unlock flow
- no mobile content-unlock feature
- no replacement of `AppTokenStore`
- no replacement of the existing fingerprint identity model
- no custom OPAQUE crypto implementation
- no silent fallback to `/api/auth/password/login`

## Recommended implementation strategy

Do not implement OPAQUE cryptography directly inside the large C++ server codebase.

Preferred strategy:

```text
pqnas_server C++ routes
  -> small OPAQUE integration wrapper
  -> isolated Rust helper or C ABI library
  -> reviewed OPAQUE implementation
```

The first safe integration model is a local helper binary:

```text
/usr/local/libexec/pqnas/pqnas_opaque_helper
```

The C++ server communicates with the helper using JSON over stdin/stdout.

Benefits:

- keeps OPAQUE crypto isolated from `routes_v5.cc`
- avoids inventing custom PAKE crypto
- allows independent tests for the helper
- allows easier replacement of the OPAQUE library later
- limits the C++ changes to routing, state, storage, and session minting

Later, if needed, the helper can be replaced with:

- a linked Rust static library exposed through a C ABI
- a small local Unix-socket service
- another reviewed implementation with the same wrapper contract

## Candidate library direction

Primary candidate:

```text
Rust opaque-ke
```

Reasons:

- implements the RFC 9807 OPAQUE protocol
- supports OPAQUE registration and login flows
- has existing Rust ecosystem support
- can be wrapped behind a helper binary or C ABI
- has WASM-related ecosystem options for browser/mobile experiments

Important rule:

```text
No production OPAQUE mode until the chosen library, suite, serialization format, and browser-client story are explicitly documented.
```

## High-level architecture

### Browser client

Responsibilities:

- show OPAQUE login UI
- run browser-side OPAQUE client code
- keep password local
- send only OPAQUE protocol messages to the server
- finish the OPAQUE login flow
- never call `/api/auth/password/login` as fallback in OPAQUE mode

### PQ-NAS C++ server

Responsibilities:

- expose HTTP endpoints
- normalize login identifiers
- apply rate limits
- load OPAQUE credential records
- create and track pending OPAQUE login state
- call the OPAQUE helper
- resolve successful OPAQUE login to a fingerprint
- check `users.json`
- mint the normal `pqnas_session`
- audit login attempts

### OPAQUE helper

Responsibilities:

- own all direct OPAQUE protocol operations
- load or receive serialized OPAQUE server setup
- process registration start/finish
- process login start/finish
- return serialized protocol responses
- return clear success/failure status
- never mint PQ-NAS sessions
- never make authorization decisions

## Storage model

### OPAQUE credential store

Recommended new file:

```text
/etc/pqnas/opaque_credentials.json
```

Example structure:

```json
{
  "version": 1,
  "accounts": [
    {
      "login": "user@example.com",
      "fingerprint": "<fingerprint_hex>",
      "opaque_password_file_b64": "<serialized_server_registration>",
      "opaque_suite": "opaque-ke-v1-ristretto255-3dh-sha512-argon2id",
      "enabled": true,
      "temporary": false,
      "created_at": "2026-06-12T00:00:00Z",
      "updated_at": "2026-06-12T00:00:00Z"
    }
  ]
}
```

Rules:

- `login` is normalized the same way as classic password login.
- `fingerprint` maps to an existing user in `users.json`.
- `enabled=false` prevents login before the OPAQUE helper is called or before session minting.
- `opaque_password_file_b64` is not a classic password hash.
- the credential record must not contain the plaintext password.
- the credential record must not contain an Argon2id password hash fallback.
- the selected OPAQUE suite must be stored to make future migrations explicit.

### OPAQUE server setup

Recommended file:

```text
/etc/pqnas/opaque_server_setup.bin
```

Rules:

- generated once per installation
- protected with strict filesystem permissions
- backed up as part of server identity/config backup
- rotation must be explicitly designed later
- losing it may invalidate OPAQUE login records depending on the selected library/suite

Recommended owner/permissions:

```text
owner: pqnas
group: pqnas
mode: 0600
```

## Endpoint plan

### Already scaffolded

```http
POST /api/auth/opaque/login/start
POST /api/auth/opaque/login/finish
```

Current behavior:

- enabled only when `PQNAS_LOGIN_MODE=opaque`
- returns fail-closed `501 opaque_backend_not_configured`
- does not mint a session
- does not accept plaintext-password fallback

### Future OPAQUE login endpoints

The scaffolded endpoints become the real login endpoints.

#### POST /api/auth/opaque/login/start

Request:

```json
{
  "login": "user@example.com",
  "credential_request_b64": "<opaque_client_login_start_message>"
}
```

Response:

```json
{
  "ok": true,
  "opaque_login_id": "<short_lived_pending_id>",
  "credential_response_b64": "<opaque_server_login_response_message>"
}
```

Server steps:

1. normalize login
2. apply rate limit
3. find OPAQUE credential record
4. if missing, run dummy/missing-user flow
5. call OPAQUE helper login-start
6. store short-lived pending state
7. return OPAQUE response

#### POST /api/auth/opaque/login/finish

Request:

```json
{
  "opaque_login_id": "<short_lived_pending_id>",
  "credential_finalization_b64": "<opaque_client_login_finish_message>"
}
```

Response on success:

```json
{
  "ok": true,
  "fingerprint": "<fingerprint_hex>",
  "role": "user",
  "expires_at": 1234567890
}
```

Server steps:

1. load pending login state
2. call OPAQUE helper login-finish
3. if cryptographic verification fails, return generic login failure
4. resolve pending login to fingerprint
5. load user from `users.json`
6. require user exists
7. require user is enabled
8. mint normal `pqnas_session`
9. return the same kind of successful login payload as classic password login

## Registration and enrollment

OPAQUE changes password provisioning.

Classic password mode can support admin-set temporary passwords.

OPAQUE mode should not treat admin-set passwords as the normal flow, because that would send or expose the user's password to an administrator or server-side component.

Preferred enrollment model:

```text
admin creates user/fingerprint
admin creates enrollment token
user opens enrollment link
browser runs OPAQUE registration locally
server stores OPAQUE credential record
```

Future endpoints:

```http
POST /api/auth/opaque/register/start
POST /api/auth/opaque/register/finish
POST /api/admin/auth/opaque/enrollment-token
```

Enrollment token rules:

- created by admin
- short-lived
- single-use
- bound to target fingerprint
- optionally bound to login/email
- stored hashed, not plaintext
- audit logged
- invalidated after use

## Password reset / recovery

OPAQUE reset should also be enrollment-token based.

Future reset flow:

```text
admin or recovery flow creates reset token
user opens reset link
browser chooses new password locally
browser performs OPAQUE registration replacement
server replaces OPAQUE credential record
```

Rules:

- reset should not reveal the new password to the server
- reset should not silently re-enable disabled users
- reset should not change fingerprint
- reset should audit old credential replacement
- reset should revoke pending OPAQUE login states for that login

## Missing login and enumeration resistance

OPAQUE mode must not reveal whether a login exists.

Rules:

- `login/start` for a missing login should return a response shaped like a normal OPAQUE start response when possible.
- `login/finish` should fail generically.
- timing should be padded or equalized as much as practical.
- error messages should not say `unknown_user`.
- audit logs may contain specific internal reasons, but public responses should not.

Recommended public error:

```json
{
  "ok": false,
  "error": "login_failed"
}
```

Internal audit examples:

```text
opaque.login_start deny missing_login
opaque.login_finish deny bad_finalization
opaque.login_finish deny disabled_user
```

## Disabled users

A disabled user may still have a valid OPAQUE credential.

Login rule:

```text
OPAQUE cryptographic success is not enough.
users.json must still allow the session.
```

Flow:

```text
OPAQUE login succeeds cryptographically
server resolves fingerprint
server checks users.json
if disabled:
  do not mint pqnas_session
  return generic login failure
  audit disabled_user
```

## Rate limiting

OPAQUE endpoints need rate limits comparable to classic password login.

Rate-limit keys:

- normalized login
- remote IP
- combined login + remote IP
- pending login id attempts

Rules:

- repeated wrong finishes should invalidate the pending login id
- pending login ids should expire quickly
- start requests should not allow unlimited memory growth
- missing-user dummy flows should still be rate-limited

## Pending login state

OPAQUE login is multi-step, so the server needs pending state.

Suggested structure:

```json
{
  "opaque_login_id": "<random_id>",
  "login": "user@example.com",
  "fingerprint": "<fingerprint_hex_or_empty_for_dummy>",
  "created_at": 1234567890,
  "expires_at": 1234567950,
  "helper_state_b64": "<serialized_server_login_state>",
  "remote_ip_hash": "<optional>",
  "user_agent_hash": "<optional>"
}
```

Rules:

- short TTL, for example 60-180 seconds
- one-time use
- removed after finish
- removed after expiry
- bounded maximum pending entries
- no plaintext password
- no browser session cookie minted before finish success

## Session minting

OPAQUE login must mint the same browser session model as existing QR/password login.

Required result:

```text
pqnas_session cookie
HttpOnly
Secure when HTTPS
SameSite=Strict
fingerprint claim
role claim
iat / exp
signed by server session key
```

Do not create a separate OPAQUE session cookie type.

## Mobile trusted-device and AppTokenStore

OPAQUE browser login does not replace the mobile trusted-device model.

Existing model:

```text
mobile pairing -> AppTokenStore -> access_token / refresh_token -> fingerprint_hex + role + device_id
```

Rules:

- do not replace `AppTokenStore`
- do not change mobile refresh token semantics as part of OPAQUE phase 1
- do not require OPAQUE for mobile pairing
- do not allow OPAQUE to bypass trusted-device revocation
- mobile app auth should continue to resolve to fingerprint independently

A later feature may allow mobile to approve OPAQUE enrollment or recovery, but that is separate from the browser-login implementation.

## Export key

OPAQUE may produce a client-side export key.

Phase 1 rule:

```text
Do not use export_key for file encryption or vault unlock yet.
```

Reason:

- login is already a large enough change
- file encryption and key wrapping require a separate threat model
- export-key misuse could create permanent data-loss or security issues

Possible later use:

```text
OPAQUE export_key -> wrap user encryption key -> encrypted vault unlock
```

That must be designed separately.

## Migration from classic password login

Phase 1:

```text
QR mode, classic password mode, and OPAQUE mode are separate configured login modes.
```

Phase 2:

```text
classic password user logs in
user chooses "Upgrade to zero-knowledge login"
browser performs OPAQUE registration
server stores OPAQUE credential for same fingerprint
admin may later disable/remove classic password credential
```

Rules:

- no silent automatic migration
- no silent fallback from OPAQUE to classic password
- migration must preserve fingerprint
- migration must not create a duplicate user
- migration should audit credential scheme changes

## Installer configuration

Supported browser login modes:

```text
PQNAS_LOGIN_MODE=qr
PQNAS_LOGIN_MODE=password
PQNAS_LOGIN_MODE=opaque
```

Recommended future installer behavior for OPAQUE:

- create `/etc/pqnas/opaque_server_setup.bin` if missing
- create `/etc/pqnas/opaque_credentials.json` if missing
- set owner to `pqnas:pqnas`
- set mode to `0600`
- refuse `PQNAS_LOGIN_MODE=opaque` unless helper is installed, unless explicitly allowing scaffold/dev mode

Possible future setting:

```text
PQNAS_OPAQUE_HELPER=/usr/local/libexec/pqnas/pqnas_opaque_helper
```

## Helper contract draft

The exact helper contract should be finalized after selecting the library.

Draft commands:

```text
server-setup-create
register-start
register-finish
login-start
login-finish
```

Example request:

```json
{
  "op": "login-start",
  "suite": "opaque-ke-v1-ristretto255-3dh-sha512-argon2id",
  "server_setup_path": "/etc/pqnas/opaque_server_setup.bin",
  "password_file_b64": "<stored_registration_or_null>",
  "credential_request_b64": "<client_message>"
}
```

Example response:

```json
{
  "ok": true,
  "credential_response_b64": "<server_message>",
  "server_login_state_b64": "<serialized_state>"
}
```

Rules:

- helper stdin/stdout only contains protocol data
- helper does not read `users.json`
- helper does not mint sessions
- helper does not decide roles
- helper logs minimally or not at all
- server owns audit logging

## Security requirements

Required:

- no plaintext password sent to server
- no custom OPAQUE crypto
- no classic password fallback in OPAQUE mode
- no session before finish success
- no session for disabled users
- generic public errors for login failure
- rate limits on start and finish
- short-lived pending state
- strict file permissions for OPAQUE server setup and credentials
- test coverage for QR/password/mobile regression

## Test plan

### Current scaffold tests

Configuration:

```text
PQNAS_LOGIN_MODE=qr
```

Expected:

- `/api/auth/config` returns `qr_enabled=true`
- OPAQUE endpoints return 404
- QR login still works

Configuration:

```text
PQNAS_LOGIN_MODE=password
```

Expected:

- `/api/auth/config` returns `password_enabled=true`
- OPAQUE endpoints return 404
- classic password login still works

Configuration:

```text
PQNAS_LOGIN_MODE=opaque
```

Expected:

- `/api/auth/config` returns `opaque_enabled=true`
- classic password form is not shown
- OPAQUE UI does not send password fallback
- OPAQUE endpoints return `501 opaque_backend_not_configured`

### Future real OPAQUE tests

- registration start/finish works
- login start/finish works
- wrong password fails
- missing login fails generically
- disabled user fails after OPAQUE success
- expired pending login id fails
- reused pending login id fails
- malformed client messages fail safely
- QR login remains unaffected
- classic password login remains unaffected
- mobile app token refresh remains unaffected
- mobile app token revoke remains unaffected
- installer creates OPAQUE files with correct permissions

## Implementation phases

### Phase 0: scaffold

Done in `feature/opaque-login-method`.

### Phase 1: design document

Add this document.

No new runtime behavior.

### Phase 2: helper skeleton

Add helper project skeleton.

No production OPAQUE yet.

Expected output:

```text
pqnas_opaque_helper --version
pqnas_opaque_helper self-test
```

### Phase 3: credential store

Add C++ storage for `opaque_credentials.json`.

No login success yet.

### Phase 4: enrollment flow

Add OPAQUE registration/enrollment.

No default production login until tested.

### Phase 5: real login flow

Implement OPAQUE login start/finish with helper.

Success mints existing `pqnas_session`.

### Phase 6: admin/user UI

Add OPAQUE enrollment/reset UI.

### Phase 7: migration

Add classic password to OPAQUE upgrade path.

## Open questions

- Which exact OPAQUE ciphersuite should be selected?
- Should the server helper be a binary, C ABI library, or local service?
- Should OPAQUE credentials live in their own file or inside a versioned auth credential store?
- How should OPAQUE server setup backup/rotation work?
- Should OPAQUE become default for new installs after classic password mode is stable?
- How should recovery words interact with OPAQUE reset?
- Should mobile trusted device be allowed to approve OPAQUE enrollment?
- Should export_key ever be used for encrypted content unlock?

## Decision checkpoint before coding real OPAQUE

Before implementing real OPAQUE, explicitly decide:

```text
library
ciphersuite
helper boundary
serialization format
credential file format
server setup storage
registration flow
reset flow
test matrix
migration rules
```

Do not proceed to production OPAQUE login until these are written down and reviewed.
