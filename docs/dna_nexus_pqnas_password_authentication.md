# DNA-Nexus / PQ-NAS Password Authentication

Password Authentication is an optional DNA-Nexus / PQ-NAS browser login mode where users sign in with a normal username/email and password while the server still keeps the internal DNA-Nexus identity model based on fingerprints.

It is designed for customer installations where QR / DNA Connect login would be a sales or adoption barrier.

Password Authentication does **not** replace the internal fingerprint identity model:
- each password-auth user maps to a DNA-Nexus fingerprint
- existing QR/DNA Connect admin fingerprints can continue to exist
- newly provisioned password users receive a CPUNK/DNA-style identity
- login attempts never create users
- user creation is explicit and admin-controlled

---

## 1) Login mode selection

DNA-Nexus / PQ-NAS separates two concepts:

- `PQNAS_AUTH_MODE`
- `PQNAS_LOGIN_MODE`

`PQNAS_AUTH_MODE` remains the internal verifier/session mode.

Current expected internal mode:

```text
PQNAS_AUTH_MODE=v5
```

`PQNAS_LOGIN_MODE` controls the browser login method:

```text
PQNAS_LOGIN_MODE=qr
```

or:

```text
PQNAS_LOGIN_MODE=password
```

### QR mode

When `PQNAS_LOGIN_MODE=qr`:

- browser login uses QR / DNA Connect
- password login UI should not be the primary login flow
- QR Auth v5 flow remains available

### Password mode

When `PQNAS_LOGIN_MODE=password`:

- browser login uses username/email + password
- QR login should be disabled for browser login
- `/api/auth/config` should report password login as enabled and QR login as disabled
- password endpoints are used for login, password change, recovery, and admin provisioning

---

## 2) Public auth configuration

The browser checks the configured login mode through:

```http
GET /api/auth/config
```

Example password-mode response:

```json
{
  "ok": true,
  "mode": "password",
  "password_enabled": true,
  "qr_enabled": false
}
```

Example QR-mode response:

```json
{
  "ok": true,
  "mode": "qr",
  "password_enabled": false,
  "qr_enabled": true
}
```

This endpoint may be public because it exposes only mode information, not secrets.

---

## 3) Internal identity model

Password Authentication still uses the same internal user identity model as the rest of DNA-Nexus / PQ-NAS.

The server identity key remains:

```text
fingerprint
```

Password credentials map:

```text
normalized login/email -> fingerprint
```

The existing `users.json` registry remains authoritative for:

- fingerprint
- role
- status
- quota
- storage allocation
- other user metadata

Password credentials are stored separately in `password_credentials.json`.

A successful password login produces the same kind of browser session as QR login:

```text
pqnas_session=<signed session cookie>
```

The browser then uses existing authenticated APIs such as:

```http
GET /api/v4/me
```

---

## 4) User creation rule

Password login attempts must never create users.

This is important because every company may have a different approval process:

- admin-created users
- pre-approved customer users
- invitation-based onboarding
- future external-user approval
- future company-specific workflow

Therefore:

```text
login attempt -> authenticate existing credential only
```

Not:

```text
login attempt -> create new user
```

If the login does not exist, authentication fails with a generic error.

---

## 5) Password credential storage

Password credentials are stored as password hashes, not plaintext passwords.

The credential record maps a normalized login to a fingerprint and a password hash.

Conceptually:

```json
{
  "login": "user@example.com",
  "fingerprint": "<fingerprint_hex>",
  "password_hash": "<argon2id encoded hash>",
  "enabled": true
}
```

Rules:

- never store plaintext passwords
- never log plaintext passwords
- never return password hashes to the client
- reject empty passwords
- enforce a reasonable maximum password length
- use Argon2id through libsodium password hashing APIs
- use the library encoded hash string format

---

## 6) Password login flow

### Browser endpoint

```http
POST /api/auth/password/login
Content-Type: application/json
```

Request:

```json
{
  "login": "user@example.com",
  "password": "user-password"
}
```

Server behavior:

1. Confirm password login mode is enabled.
2. Normalize login.
3. Apply rate limits:
   - per IP
   - global per login
4. Look up password credential by normalized login.
5. Verify password with Argon2id.
6. Resolve credential fingerprint.
7. Load user from `users.json`.
8. Require user exists.
9. Require user status is enabled.
10. Mint signed browser session cookie.
11. Return safe JSON response.

Example success response:

```json
{
  "ok": true,
  "fingerprint": "<fingerprint_hex>",
  "role": "user",
  "expires_at": 1780914588
}
```

Example failure response:

```json
{
  "ok": false,
  "error": "invalid_login_or_password"
}
```

The failure response should not reveal whether the login exists.

---

## 7) Session cookie

On successful password login, the server emits:

```http
Set-Cookie: pqnas_session=<signed-cookie-token>; Path=/; HttpOnly; SameSite=Strict; Secure
```

Cookie rules:

- `HttpOnly`
- `Secure`
- `SameSite=Strict`
- short lifetime
- signed by server key
- verified on every authenticated request

The session cookie contains a fingerprint claim, issued-at time, and expiry.

If session claims are hand-built as JSON, every interpolated value must be strictly validated before minting and after parsing.

The fingerprint claim must not allow JSON injection characters such as:

- `"`
- `\`
- control characters
- braces
- unexpected encodings

---

## 8) Missing login timing resistance

A missing login should not be trivially distinguishable from an existing login with a wrong password by timing.

Password verification should avoid this pattern:

```text
missing login -> return immediately
existing login -> run Argon2id verify
```

Preferred behavior:

- if login is missing, run a dummy Argon2id verification and then fail
- if login is disabled or has no usable hash, run a dummy Argon2id verification and then fail
- dummy verification must never authenticate the user
- crypto return values marked `warn_unused_result` must be consumed

This reduces account enumeration through timing.

It does not make all timing perfectly identical, but it removes the most obvious deterministic timing oracle.

---

## 9) Password change flow

Authenticated users should be able to change their own password.

Endpoint:

```http
POST /api/auth/password/change
Content-Type: application/json
Cookie: pqnas_session=...
```

Request:

```json
{
  "login": "user@example.com",
  "current_password": "old-password",
  "new_password": "new-password"
}
```

Server behavior:

1. Require valid session cookie.
2. Require password mode.
3. Normalize login.
4. Verify current password.
5. Ensure credential fingerprint matches the authenticated session fingerprint.
6. Hash new password with Argon2id.
7. Replace stored credential hash.
8. Return success.

Important:

A user must not be able to change another user's password by submitting another login.

---

## 10) Admin password set/reset

Admins need a way to set or reset passwords for existing users.

Endpoint:

```http
POST /api/auth/password/set
Content-Type: application/json
Cookie: pqnas_session=...
```

Server behavior:

1. Require valid session cookie.
2. Require admin role.
3. Require target fingerprint or login.
4. Require target user exists.
5. Hash new password.
6. Update or create credential mapping for the target identity.
7. Return success.

This is an administrative recovery/control path and should be audit-logged.

---

## 11) Admin password user creation

Admins can explicitly create password-auth users.

Endpoint:

```http
POST /api/admin/users/password-create
Content-Type: application/json
Cookie: pqnas_session=...
```

Request:

```json
{
  "name": "Example User",
  "login": "user@example.com",
  "password": "temporary-long-password",
  "role": "user",
  "status": "enabled",
  "quota_bytes": 10737418240
}
```

Server behavior:

1. Require valid admin session.
2. Confirm password mode is enabled.
3. Normalize login.
4. Reject duplicate login.
5. Generate a new CPUNK/DNA-style identity.
6. Derive fingerprint from the generated public key.
7. Create user record in `users.json`.
8. Hash password and create credential in `password_credentials.json`.
9. Return recovery words once.
10. Clear recovery phrase copies from application-owned memory as soon as practical.

Example response:

```json
{
  "ok": true,
  "login": "user@example.com",
  "fingerprint": "<fingerprint_hex>",
  "role": "user",
  "status": "enabled",
  "quota_bytes": 10737418240,
  "recovery_words": "word1 word2 ... word24",
  "recovery_words_shown_once": true,
  "warning": "Recovery words are shown once and are not stored by the server."
}
```

The recovery words are shown once because the server does not store them.

---

## 12) Generated password-user identity

New password-auth users receive a CPUNK/DNA-style identity.

Conceptual derivation:

```text
24 recovery words
-> deterministic ML-DSA-87 keypair
-> public key
-> fingerprint = SHA3-512(public key)
```

The server stores:

- fingerprint
- public identity metadata as needed
- user record
- password credential mapping

The server must not store:

- 24 recovery words
- private key
- plaintext password

This gives password-created users a real DNA-Nexus identity, not a fake placeholder fingerprint.

---

## 13) First-admin bootstrap for password installs

Fresh password-mode installs need a safe first-admin path.

If `PQNAS_LOGIN_MODE=password` is selected during installation, the installer should provide a temporary bootstrap token.

Environment example:

```text
PQNAS_AUTH_MODE=v5
PQNAS_LOGIN_MODE=password
PQNAS_PASSWORD_BOOTSTRAP_TOKEN=<temporary-random-token>
```

Bootstrap endpoint:

```http
POST /api/auth/password/bootstrap-admin
Content-Type: application/json
X-PQNAS-Bootstrap-Token: <temporary-random-token>
```

Request:

```json
{
  "login": "admin@example.com",
  "password": "change-this-long-password",
  "name": "Admin"
}
```

Server behavior:

1. Require password mode.
2. Require bootstrap token configured.
3. Apply rate limits:
   - per IP
   - global bootstrap limit
4. Compare token.
5. If an enabled admin already exists:
   - attach credential to existing admin fingerprint when appropriate.
6. If no admin exists:
   - generate CPUNK/DNA-style identity
   - create enabled admin user
   - create password credential
   - return recovery words once
7. Instruct admin to remove the bootstrap token after use.

After successful bootstrap:

```bash
sudo sed -i '/^PQNAS_PASSWORD_BOOTSTRAP_TOKEN=/d' /etc/pqnas/pqnas.env
sudo systemctl restart pqnas.service
```

Bootstrap must not leave a permanent backdoor.

---

## 14) Password recovery with 24 words

Password recovery lets a user reset their password using their login and 24 recovery words.

Endpoint:

```http
POST /api/auth/password/recover
Content-Type: application/json
```

Request:

```json
{
  "login": "user@example.com",
  "recovery_words": "word1 word2 ... word24",
  "new_password": "new-long-password"
}
```

Server behavior:

1. Require password mode.
2. Normalize login.
3. Apply rate limits:
   - per IP
   - global per login
4. Find credential by login.
5. Derive identity from submitted recovery words.
6. Compare derived fingerprint to credential fingerprint.
7. Require fingerprints match.
8. Require account is not disabled/pending for login.
9. Hash the new password.
10. Replace password hash.
11. Return safe success response.

Recovery must not:

- create a user
- enable a disabled user
- approve a pending user
- reveal whether the login exists
- return recovery words
- store submitted recovery words

Example success response:

```json
{
  "ok": true,
  "login": "user@example.com",
  "account_status": "enabled",
  "login_allowed": true
}
```

---

## 15) Recovery phrase handling

Recovery phrases are one-time secrets.

Rules:

- show recovery words once
- do not store recovery words on the server
- do not log recovery words
- do not include recovery words in audit logs
- do not include recovery words in user activity
- do not commit test recovery words
- clear application-owned recovery phrase strings as soon as practical
- clear recovery phrase strings on success and error paths
- clear partially generated recovery phrase strings if identity generation fails

When returning recovery words:

- serialize them as late as practical
- avoid storing them in generic JSON objects longer than needed
- clear intermediate strings after sending the response when practical
- document unavoidable framework or allocator copies

Known limitation:

Standard library strings, JSON temporary objects, allocator buffers, and HTTP response buffers may briefly hold copies. Eliminating every copy requires a larger secure allocator or custom response streaming design.

---

## 16) Multi-file consistency

Password user creation updates more than one persistent file:

- `users.json`
- `password_credentials.json`

This can fail halfway.

Dangerous case:

```text
users.json save succeeds
password_credentials.json save fails
```

Result:

```text
orphan user without credential
```

Rules:

- treat multi-file state updates as non-atomic
- order writes deliberately
- rollback newly created users if credential save fails
- audit-log rollback failures
- fail closed if final state is not safe
- make repair/reconciliation possible after crashes

Best-effort rollback is acceptable, but failure must be visible to administrators.

---

## 17) Rate limiting

Password authentication endpoints must be rate-limited.

Recommended patterns:

### Login

```text
per IP + per normalized login global limit
```

### Recovery

```text
per IP + per normalized login global limit
```

### Bootstrap

```text
per IP + global bootstrap limit
```

### Admin password operations

```text
admin session required + route-level rate limit where practical
```

Rate-limited responses should use:

```http
HTTP 429 Too Many Requests
Retry-After: <seconds>
```

User-facing errors should stay generic.

Audit logs may include structured reason codes such as:

```text
rate_limited
global_rate_limited
```

---

## 18) Audit behavior

Password-auth events should be audit-logged without logging secrets.

Good audit events:

- password login success/failure
- password change
- password set/reset by admin
- password recovery success/failure
- bootstrap admin success/failure
- password user creation success/failure
- rate limit events
- rollback failure

Do not log:

- plaintext passwords
- password hashes
- recovery words
- bootstrap tokens
- session cookies
- raw Authorization headers

Audit reason codes should be structured but safe.

---

## 19) UI behavior

Password mode UI should include:

- login form
- forgot password / recovery form
- password change UI for users
- password change UI for admins if admin settings use a separate page
- admin password user creation UI
- clear one-time recovery words warning
- translated strings for supported languages

The UI must not be the security boundary.

Backend checks must enforce:

- login mode
- session validity
- role
- account status
- ownership/fingerprint match
- rate limits

When translating login forms, do not use `label.textContent` if the `<label>` contains an `<input>` child, because it removes the input from the DOM.

---

## 20) Error responses

Password authentication should avoid user enumeration.

Prefer generic errors:

```json
{
  "ok": false,
  "error": "invalid_login_or_password"
}
```

Avoid public distinctions such as:

```text
user_not_found
wrong_password
credential_missing
disabled_but_password_correct
recovery_words_wrong_for_existing_login
```

Admin-only endpoints may return more specific errors when the caller is already authenticated as admin, but should still avoid leaking secrets.

---

## 21) Security notes

- Password Authentication is a browser login method, not a new internal identity system.
- Fingerprints remain the internal identity anchor.
- Recovery words prove access to the generated DNA identity.
- Password hashes only authenticate the browser login.
- QR and password login modes must be explicitly gated by config.
- Recovery can reset a password but must not approve or enable an account.
- Bootstrap tokens must be temporary and removed after first admin setup.
- Password-created users should receive real CPUNK/DNA-style identities.

---

## Summary

Password Authentication makes DNA-Nexus / PQ-NAS easier to sell and deploy in environments where QR / DNA Connect login is too unfamiliar, while preserving the fingerprint-based internal identity model.

The core rule is:

```text
username/email + password authenticates access to an existing fingerprint;
it does not replace the fingerprint identity model.
```
