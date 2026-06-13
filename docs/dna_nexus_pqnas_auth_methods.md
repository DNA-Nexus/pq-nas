# DNA-Nexus / PQ-NAS Authentication Methods

## Purpose

DNA-Nexus / PQ-NAS supports multiple browser login methods while keeping one internal identity and authorization model.

The internal identity anchor remains:

```text
fingerprint
```

A login method is only an entry path into that fingerprint-backed session model.

## Current and planned browser login modes

### 1) QR / DNA Connect

Environment:

```text
PQNAS_AUTH_MODE=v5
PQNAS_LOGIN_MODE=qr
```

Purpose:

- browser shows QR
- DNA Connect or a compatible app approves the login
- server mints `pqnas_session`
- session contains the internal fingerprint claim

### 2) Classic password login

Environment:

```text
PQNAS_AUTH_MODE=v5
PQNAS_LOGIN_MODE=password
```

Purpose:

- browser shows username/email + password form
- server verifies Argon2id credential from `password_credentials.json`
- credential maps login to fingerprint
- server mints `pqnas_session`

### 3) OPAQUE zero-knowledge password login

Environment:

```text
PQNAS_AUTH_MODE=v5
PQNAS_LOGIN_MODE=opaque
```

Purpose:

- browser shows OPAQUE zero-knowledge password login
- browser must not send plaintext password to the server
- server stores an OPAQUE credential record, not a classic password hash
- successful OPAQUE authentication resolves to fingerprint
- server mints the same `pqnas_session` cookie used by the other browser login modes

Current status:

    server-side OPAQUE login and browser-side OPAQUE client integration are implemented

The current implementation includes:

- server-side OPAQUE registration/enrollment handling
- helper-backed OPAQUE login transcript handling
- browser-side OPAQUE client module integration
- OPAQUE login UI in browser login mode
- pqnas_session minting after successful OPAQUE transcript verification
- enabled-user check before session minting
- /api/v4/me verification before redirecting to /app

Production readiness depends on keeping the selected helper version, browser client, protocol suite, serialization format, and stored credential format tested together.

The login UI must continue to fail closed if the browser OPAQUE module is missing, incompatible, or not loaded.

## Shared success result

All successful browser login methods converge to:

```text
fingerprint -> pqnas_session
```

This means these subsystems must not care which login method was used:

- File Manager
- Gallery apps
- admin/user role checks
- quotas
- storage pool assignment
- workspace access
- Circle Stack / Echo Stack / Reel Stack authorization
- app launcher permissions

## Mobile trusted-device model

DNA-Nexus Mobile uses the existing trusted device / bearer token model.

Mobile pairing and app tokens are separate from browser login mode:

```text
trusted mobile device -> access_token / refresh_token -> fingerprint_hex + role + device_id
```

OPAQUE browser login must not replace `AppTokenStore`, trusted device records, refresh tokens, or mobile bearer-token verification.

## Browser OPAQUE client rule

OPAQUE mode uses a browser-side OPAQUE client.

Rules:

- the browser performs OPAQUE client cryptographic steps locally
- the browser must never send plaintext password fields to OPAQUE endpoints
- the OPAQUE browser module must fail closed if missing or incompatible
- OPAQUE mode must not silently call /api/auth/password/login
- the browser may redirect to /app only after /api/v4/me confirms that the standard pqnas_session works

Server-side OPAQUE readiness and browser-side OPAQUE client compatibility must be tested together before treating a build as production-ready.


## Fail-closed rule

If `PQNAS_LOGIN_MODE=opaque` is selected but the OPAQUE backend or browser-side OPAQUE client is not fully wired:

- do not show the classic password form as fallback
- do not send plaintext password to the server
- return a clear not-configured error
- keep QR/password modes disabled unless explicitly configured

This prevents a half-finished OPAQUE UI from silently weakening security.
