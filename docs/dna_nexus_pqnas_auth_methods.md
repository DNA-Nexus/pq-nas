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

```text
scaffold only
```

The routes exist as fail-closed placeholders until the reviewed OPAQUE crypto backend and browser-side client are integrated.

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

## Fail-closed rule

If `PQNAS_LOGIN_MODE=opaque` is selected but the OPAQUE backend is not fully wired:

- do not show the classic password form as fallback
- do not send plaintext password to the server
- return a clear not-configured error
- keep QR/password modes disabled unless explicitly configured

This prevents a half-finished OPAQUE UI from silently weakening security.
