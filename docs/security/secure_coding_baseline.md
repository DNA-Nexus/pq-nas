# DNA-Nexus / PQ-NAS Secure Coding Baseline

## Purpose

This document defines baseline secure coding rules for DNA-Nexus / PQ-NAS.

The goal is to prevent common vulnerability classes from being introduced during normal development. These rules apply to server code, bundled apps, installer code, helper scripts, and security-sensitive tooling.

This is a practical engineering checklist, not a complete security policy.

---

## 1. Do not use shell command strings for new code

Avoid adding new uses of:

- `popen()`
- `system()`
- `/bin/sh -c`
- shell command strings assembled with `+`
- command execution that depends on shell quoting for safety

Shell command strings are fragile because the shell interprets spaces, quotes, semicolons, pipes, redirects, variables, glob patterns, and other special syntax.

### Preferred pattern

When an external program must be executed, use an argument-vector based execution model:

- build an `argv` list
- use `fork()`
- redirect stdout/stderr through a pipe if output is needed
- use `execvp()` or equivalent
- use `waitpid()`
- limit captured output size
- apply a timeout
- never pass user-controlled strings through a shell

Example concept:

```text
program: /usr/local/bin/nodus-cli
argv:
  - /usr/local/bin/nodus-cli
  - -i
  - /srv/pqnas/config/nodus/identity
  - identity-init
```

Avoid this:

```bash
/bin/sh -c "timeout 5 /usr/local/bin/nodus-cli -i '/srv/pqnas/config/nodus/identity' identity-init 2>&1"
```

The external tool may still be used, but it must be executed as a program with arguments, not as shell code.

---

## 2. Treat all paths and device names as dangerous input

This applies even when the value appears to come from local system state.

Be careful with:

- filenames
- workspace paths
- mountpoints
- storage pool IDs
- disk names
- device paths
- archive paths
- temporary paths
- paths received from another NAS
- paths stored in JSON metadata

Rules:

- normalize paths before use
- enforce allowed roots
- reject path traversal
- reject unexpected absolute paths
- avoid following symlinks unless explicitly intended
- do not pass paths through shell command strings
- do not trust values only because they came from a database

---

## 3. Symlink safety

Symlinks are a common source of file access bugs.

Do not assume that a path remains safe just because its string looks safe.

Rules:

- use `symlink_status()` when you need to inspect the path itself
- use `status()` only when following symlinks is intentional
- reject symlinks in upload, restore, delete, export, archive, and install paths unless explicitly supported
- do not perform privileged operations on paths that may be symlinks
- do not allow users to create symlinks that can point outside their allowed root
- do not archive, preview, restore, or delete symlink targets outside the allowed root
- when opening files, prefer descriptor-based checks where practical

Dangerous pattern:

```text
validate path string
later open/remove/copy path
```

Safer pattern:

```text
resolve allowed root
open/check using filesystem APIs
verify the final target is still inside the allowed root
avoid following symlinks unless explicitly intended
```

---

## 4. Avoid TOCTOU bugs

TOCTOU means "time of check to time of use".

A bug can happen when code checks a path or state, then later uses it after an attacker or another process has changed it.

Dangerous pattern:

```text
if file exists and is safe:
    open file later
```

Between the check and the open, the file may have been replaced.

Rules:

- avoid separate check-then-use sequences for security-sensitive paths
- prefer opening first, then validating the opened object
- prefer file descriptors over repeated path lookups
- re-check state immediately before sensitive actions
- keep lock coverage across check and mutation when possible
- use atomic rename/write patterns for durable files
- use compare-and-set style state transitions for job/status records
- do not trust cached authorization or visibility state for later media/file serving

Examples where TOCTOU matters:

- share access
- trash restore/purge
- file delete/move/copy
- gallery export
- archive extraction
- update apply
- storage operations
- media preview cache
- public/private visibility changes
- workspace membership checks

---

## 5. Prefer data-local temporary directories

Do not use global `/tmp` for sensitive or user-data-related staging unless there is a specific reason.

Preferred locations:

- `PQNAS_TMP_DIR`
- `<PQNAS_ROOT>/tmp`
- `/srv/pqnas/tmp`

Temporary files should be:

- created with unique names
- owned by the service user or root as appropriate
- cleaned up after use
- created under a directory with controlled permissions
- protected from symlink races
- not shared with unrelated system users

---

## 6. Authentication and authorization must be explicit

Every route must clearly define who can call it.

For user/admin APIs:

- require a valid session cookie or token
- check user status
- check user role
- check workspace membership where relevant
- check whether the account is enabled
- avoid relying on UI visibility as a security boundary

Admin routes must use admin authorization helpers.

Mutation routes using cookies should also enforce same-origin protection.

Login attempts must never create users. User creation must be explicit, auditable, and tied to a provisioning flow such as admin user creation, bootstrap, invitation, or future approved onboarding.

Recovery flows must not create users and must not enable disabled or pending users unless a separate explicit admin-approved state transition is performed.

---

## 7. Rate-limit sensitive and expensive endpoints

Add rate limits to endpoints that are:

- login or verification related
- token/session related
- password recovery related
- bootstrap or first-admin related
- upload related
- update/install related
- federation related
- media-preview related
- CPU-heavy
- disk-heavy
- network-heavy
- capable of spawning external processes

Rate-limit keys should normally include client IP and route/action name.

For account-bound flows, use both:

- per-IP limits, to stop one client from hammering
- global per-login or per-token limits, to slow distributed attempts from many IPs

Examples:

- password login: per-IP + per-login global limit
- password recovery: per-IP + per-login global limit
- bootstrap token: per-IP + global bootstrap limit
- invite token: per-IP + per-token global limit

Rate-limited responses should return HTTP 429 and a safe generic error.

Audit logs may distinguish local rate limits from global rate limits, but user-facing errors should stay generic.

---

## 8. Cookie defaults

Cookies should default to:

- `HttpOnly`
- `Secure`
- `SameSite=Strict`

Use `SameSite=Lax` only when the flow genuinely requires it.

Sensitive cookies should have:

- short lifetime
- rotation support for signing/encryption keys
- invalidation behavior after key rotation
- no exposure to JavaScript unless absolutely required

Cookie claim construction must not rely on unsafe string concatenation with unvalidated input.

If hand-built JSON is unavoidable:

- validate every interpolated value with a strict allowlist
- reject quotes, backslashes, braces, control characters, and unexpected encodings
- prefer JSON library serialization over manual concatenation
- verify parsed claims after decoding
- fail closed on malformed or unexpected claim fields

---

## 9. Secrets must never enter the repository

Never commit:

- private keys
- cookie signing keys
- API tokens
- `.env` files containing secrets
- `keys.env`
- production `pqnas.env`
- certificates with private material
- bootstrap tokens
- invite tokens
- recovery phrases
- test recovery words
- test passwords
- debug dumps containing credentials

Pre-commit scanning is a guardrail, not a replacement for care.

The scanner should block real secrets without blocking normal feature flags.

---

## 10. Environment and key files must have strict permissions

Environment files that may contain secrets or security-sensitive configuration should be installed with restrictive permissions.

Recommended default:

```text
owner: root
group: root
mode: 0600
```

Do not print env file contents during debugging.

Use `stat` or `ls -l` to verify permissions without exposing values.

Temporary bootstrap tokens must be removed after first use.

If an installer writes a bootstrap token into an env file, the installer and documentation must clearly tell the admin to remove it after the first admin account has been created.

---

## 11. Installer changes are part of security fixes

A runtime security fix is incomplete if a fresh install still produces unsafe defaults.

When adding a security-related env variable, directory, systemd directive, helper, permission rule, or config default, update the installer too.

Examples:

- systemd hardening
- env file permissions
- Nodus seed configuration
- Update Center helper paths
- runtime directories
- static asset install paths
- service user ownership
- auth mode selection
- bootstrap token handling
- first-admin setup flow

Fresh installs must work securely without undocumented manual edits.

Installer choices must map clearly to runtime behavior. For example, internal verifier/session mode and browser login mode should be separate settings when they control different security behaviors.

---

## 12. Avoid hardcoded production-only values

Hardcoded values may be acceptable as recovery or development fallbacks, but production behavior should be configurable without rebuilding.

Preferred pattern:

1. Read env/config override.
2. Use installer-provided secure default.
3. Fall back to compiled defaults only if no config is present.

Examples:

- public base URL
- Nodus seed list
- tool paths
- timeout values
- runtime directories
- auth/login mode
- bootstrap token behavior

---

## 13. Federation safety rules

Federation data must be treated as untrusted remote input.

Required controls:

- pre-parse size limits
- strict JSON parsing
- canonical JSON for signatures
- cryptographic signatures for events
- public key / origin identity binding
- verification before storing or applying
- rejection of unsigned or invalid events
- pruning or caps for unbounded tables
- safe handling of media references
- no direct trust in remote-provided paths

Invalid federation data should be rejected and audit-logged.

---

## 14. Media and preview generation

Media-preview generation is resource-sensitive.

Rules:

- validate visibility before serving cached previews
- re-check current DB state before returning media
- use concurrency limits
- use timeouts
- cap output sizes
- avoid leaking filesystem paths
- serve safe placeholders when preview generation fails
- do not let private media become public through stale cache

---

## 15. Update Center safety rules

Update/install features must be fail-closed.

Rules:

- verify package hash
- verify plan hash
- use canonical JSON consistently
- validate install plan before apply
- separate dry-run from apply
- keep apply helper tightly scoped
- log install state transitions
- avoid printing secrets or raw helper output to user-facing activity
- require admin authorization
- rate-limit sensitive endpoints

Unicode and non-ASCII paths must be covered by regression tests when used in hashing or canonicalization.

---

## 16. Audit log and user activity are different things

Audit logs may contain technical security metadata.

User-facing activity should contain only safe, understandable summaries.

Do not blindly copy audit details into My Activity.

Avoid exposing:

- raw command lines
- stdout/stderr dumps
- filesystem internals
- private fingerprints unless required
- tokens
- session IDs
- secret config values
- private media paths
- recovery phrases
- password reset metadata that reveals account existence

---

## 17. Logging rules

Do not log sensitive values.

Avoid logging:

- full user fingerprints unless needed
- session IDs
- tokens
- private keys
- cookie keys
- invite secrets
- bootstrap tokens
- recovery phrases
- raw passwords
- raw Authorization headers
- full paths to private files
- raw external command output

When logging identifiers, prefer shortened or hashed forms if the full value is not required.

Authentication failures should use safe, generic user-facing errors. Audit logs may contain structured reason codes, but must not include passwords, recovery phrases, tokens, or session values.

---

## 18. Database and table growth controls

Any table that can grow from user or remote activity needs a cap, pruning rule, or retention policy.

Especially watch:

- federation events
- reactions
- notifications
- audit-like side tables
- preview caches
- temporary job records
- upload records
- failed attempts
- rate-limit buckets
- login/recovery attempt records
- invitation/bootstrap records

Unbounded tables become a denial-of-service risk.

In-memory rate-limit maps must have cleanup behavior or a bounded size. Attackers may use random login names or tokens to create many unique buckets.

---

## 19. External process output must be bounded

Whenever capturing output from an external process:

- cap maximum bytes
- truncate safely
- mark truncation
- avoid returning raw output to normal users
- sanitize before audit/activity
- do not assume external tool output is safe JSON or safe text

---

## 20. Prefer small security commits

Security fixes should be easy to review.

Preferred workflow:

1. verify current code
2. patch one finding or one subfinding
3. build
4. run focused test
5. commit with clear message
6. push
7. continue to next finding

Avoid combining unrelated security changes into one large commit.

When multiple small fixes are related to the same red-team finding, keep the commit message clear enough that a reviewer can map it back to the finding.

---

## 21. Add tests or guard scripts when possible

If a bug class can return later, add a guard.

Useful guard types:

- regression tests
- route tests
- static grep-style dev checks
- pre-commit checks
- installer permission checks
- dependency version checks
- canonicalization tests
- auth-mode gating tests
- password recovery tests
- timing-oracle smoke checks
- secret scanning for recovery words and test passwords

A small guard is often better than a comment.

---

## 22. Dependency and supply-chain hygiene

Avoid floating dependency ranges for security-sensitive packages.

Prefer:

- pinned native dependency minimum versions
- pinned Dart/Flutter crypto dependency versions
- committed lockfiles where appropriate
- build failure on unsupported old versions

Crypto libraries should not float silently across versions unless there is a deliberate update process.

---

## 23. Systemd hardening

The service unit should use defense-in-depth hardening where compatible with current functionality.

Preferred directives include:

- `PrivateTmp=yes`
- `ProtectHome=yes`
- `ProtectSystem=strict`
- explicit `ReadWritePaths=...`

`NoNewPrivileges=yes` should be enabled when the service no longer requires tightly scoped sudo helpers.

If the service still requires helper commands, document the reason and keep the helper boundary narrow.

---

## 24. When shell execution still exists

Some legacy paths may still use `popen()` or `system()`.

Do not copy those patterns into new code.

When touching those areas, prefer incremental migration to:

- dedicated helper functions
- `fork + execvp`
- explicit argv arrays
- bounded capture
- clear allowlists
- documented sudo helper boundaries

Legacy shell usage should shrink over time.

---

## 25. Password authentication rules

Password authentication must preserve the internal DNA-Nexus identity model.

Rules:

- internal user identity remains fingerprint-based
- login/email maps to a fingerprint
- login attempts must never create users
- admin/user provisioning creates users explicitly
- recovery must not create users
- recovery must not enable disabled or pending users
- password mode must not accidentally allow QR login if QR is disabled by config
- QR mode must not accidentally expose password endpoints as an unintended login path
- `/api/auth/config` may expose mode information only if it does not leak secrets

Password storage rules:

- store password hashes only, never plaintext
- use Argon2id or an approved password hashing scheme
- use library-provided password hash string formats when available
- enforce reasonable password length limits
- reject empty passwords
- avoid logging passwords or password hashes

Password verification rules:

- missing login and wrong password paths should have similar timing
- disabled or empty-hash accounts should not create a strong account-existence timing oracle
- if a dummy password verify is used, it must always fail authentication
- dummy verify return values must be consumed if the crypto library marks them `warn_unused_result`

---

## 26. Recovery phrases and one-time secrets

Recovery phrases, bootstrap tokens, invite tokens, and similar values are one-time or highly sensitive secrets.

Rules:

- show recovery phrases once
- do not store recovery phrases on the server
- do not log recovery phrases
- do not include recovery phrases in audit logs
- do not include recovery phrases in user activity
- do not include recovery phrases in screenshots, sample docs, or committed test logs
- clear application-owned memory copies as soon as practical
- clear recovery phrase copies on success and on error paths
- clear partially generated recovery phrases if identity generation fails
- document any unavoidable framework-level copies

When returning a one-time secret to the client:

- build the response carefully
- avoid storing the secret in long-lived objects
- avoid storing the secret inside generic JSON objects longer than needed
- serialize the secret as late as practical
- clear intermediate strings after sending the response when possible
- accept that the HTTP framework may hold an internal copy until the response is sent

Known limitation:

Some transient copies may still exist in standard library strings, JSON temporary objects, allocator buffers, or HTTP framework response buffers. These should be minimized and documented, but eliminating them fully may require secure allocators or custom response streaming.

---

## 27. Multi-file state updates need rollback or repair

Security-sensitive operations often update more than one persistent file.

Examples:

- `users.json`
- `password_credentials.json`
- shares/config files
- workspace metadata
- app manifests
- update state files
- audit/activity side records

If one save succeeds and a later save fails, the system can be left in a half-created or inconsistent state.

Rules:

- identify every multi-file mutation
- order writes deliberately
- use atomic write/rename helpers
- apply best-effort rollback when possible
- audit-log rollback failures
- make repair/reconciliation possible after a crash
- fail closed if the final state is not safe
- avoid granting access until all required state has been written successfully

Example:

If user creation writes `users.json` first and `password_credentials.json` second, then failure in the second write must roll back the new user or leave the account disabled and unauthenticated.

---

## 28. JSON construction rules

Prefer library-based JSON construction and serialization.

Avoid hand-built JSON strings.

If hand-built JSON is unavoidable:

- only interpolate values validated by strict allowlists
- use JSON serialization for string values
- reject dangerous characters before interpolation
- ensure the final output is valid JSON
- do not concatenate unescaped user input into JSON
- avoid storing secrets in generic JSON DOM objects longer than necessary
- clear secret-bearing serialized strings after use when possible

For security-sensitive claims such as session cookies:

- validate claim fields before minting
- validate claim fields after parsing
- reject malformed or unexpected encodings
- prefer compact allowlists over broad string acceptance

---

## 29. Bootstrap and first-admin setup

Fresh installs must not require undocumented manual authentication setup.

If password login mode is selected during installation:

- the installer must provide a safe first-admin bootstrap path
- bootstrap tokens must be random and high entropy
- bootstrap tokens must be temporary
- bootstrap endpoints must be disabled when no token is configured
- bootstrap endpoints must be rate-limited per IP and globally
- bootstrap must create a real internal identity, not a fake placeholder
- bootstrap must return recovery words once
- bootstrap must clearly instruct the admin to remove the token after use

Bootstrap must not:

- create users from ordinary login attempts
- leak whether a guessed token was close
- store recovery phrases
- leave enabled admin access with missing credentials
- silently fall back to unsafe default credentials

---

## 30. Timing and enumeration resistance

Authentication and recovery endpoints must avoid easy account enumeration.

Rules:

- use generic errors for invalid login/password combinations
- avoid distinct user-facing errors for missing user vs wrong password
- avoid fast-return paths that clearly distinguish existing and missing accounts
- use dummy password verification where practical
- rate-limit both per IP and per account/login
- audit reason codes internally without exposing them to attackers
- avoid returning account status details before authentication

Some timing differences may remain due to system load, storage access, network latency, or cache state. The goal is to remove obvious deterministic timing oracles.

---

## 31. Default decision rule

When uncertain, choose the safer default:

- deny instead of allow
- private instead of public
- bounded instead of unbounded
- configured instead of hardcoded
- argv execution instead of shell execution
- explicit authorization instead of implicit trust
- sanitized summary instead of raw technical detail
- installer-backed default instead of manual setup
- temporary secret instead of permanent bootstrap access
- rollback/repair instead of inconsistent partial state

---

---

## 32. Shared state files need shared locking

If more than one route, module, translation unit, helper, or process can
read-modify-write the same persistent state file, the locking mechanism must
be shared by all writers.

Do not rely only on a file-scope `static std::mutex` if another source file can
update the same file.

Rules:

- identify every writer for a security-sensitive state file
- use one shared locking strategy for all writers
- use file-level locking when the same file can be touched from multiple
  compilation units or processes
- document lock ordering when combining in-process mutexes with file locks
- keep lock coverage across load, validation, mutation, and save
- avoid updating the same JSON file through duplicated helper logic in multiple
  modules

Example:

```text
routes_v5.cc updates opaque_enrollments.json
main.cpp also invalidates opaque_enrollments.json during user revoke

Both paths must coordinate through the same lock file.
```

Preferred pattern:

```text
in-process mutex for same-module thread serialization
+
file-level lock for cross-module/cross-process coordination
+
atomic temp-file + rename save
```

---

## 33. One-time token lifecycle must follow account state changes

Setup links, reset links, enrollment tokens, invite tokens, bootstrap tokens,
and recovery tokens are security-sensitive credentials.

Rules:

- store only a hash of the token when practical
- give tokens a short expiration time
- mark tokens as used before completing irreversible credential changes when
  replay risk matters
- invalidate older active tokens when issuing a replacement token
- invalidate active tokens when the target user is revoked or security state
  changes
- bind tokens to the intended user, fingerprint, login, or purpose
- do not allow multiple active reset/setup links unless there is an explicit
  reason
- audit token creation, use, replacement, invalidation, and rollback failures
- never log plaintext tokens
- never return plaintext tokens after a partial failure

Examples:

```text
Creating a new password reset token should invalidate old reset tokens for that user.

Revoking a user should invalidate active setup/reset tokens for that fingerprint.

If reset-token creation succeeds but a later credential change fails, the new
token must be invalidated before returning an error.
```

---

## 34. Sensitive multi-step UI flows should become single backend operations

Do not let the browser orchestrate security-sensitive state transitions through
multiple independent HTTP calls when partial success can leave the account in an
unsafe or confusing state.

Dangerous pattern:

```text
1. frontend disables old credential
2. frontend creates reset token
```

If step 1 succeeds and step 2 fails, the user may be locked out.

Preferred pattern:

```text
POST /api/admin/auth/.../force-reset
```

The backend owns the full sequence:

1. validate admin authorization
2. validate target user and credential state
3. create or prepare the replacement token
4. perform the credential/account transition
5. roll back or invalidate temporary state if a later step fails
6. return the plaintext one-time token only on full success

Rules:

- the backend must define the safe ordering
- partial failure must not grant access
- partial failure must not leave a valid one-time token exposed
- partial failure must be audit logged
- the user-facing/admin-facing error should be clear enough for repair
- old low-level endpoints should be removed, restricted, or documented as
  dangerous escape hatches

---

## 35. Browser cryptography must fail closed

Browser-side cryptographic login modules, such as future OPAQUE browser clients,
must fail closed.

Rules:

- do not load authentication crypto from a CDN
- serve crypto modules as local, versioned static assets
- pin the protocol suite, serialization format, and compatible helper version
- reject incompatible browser-helper protocol versions
- do not fall back to classic password login when crypto fails to load
- do not send plaintext passwords to server-side OPAQUE endpoints
- do not store password material or client protocol state in `localStorage`,
  `sessionStorage`, IndexedDB, URLs, logs, or DOM attributes
- allow only one active login attempt per UI instance
- discard client state after success, failure, timeout, or navigation
- confirm the final session with the normal user/session endpoint before
  redirecting to the app

A missing or incompatible browser crypto module should be treated as
authentication-method unavailable, not as a reason to use a weaker fallback.


## 36. Mobile client credential storage and backup safety

Mobile clients must treat access tokens, refresh tokens, device identifiers,
pairing state, server origins, private keys, recovery material, and cached
authorization state as secrets.

Rules:

- do not store live tokens in plaintext `SharedPreferences`, DataStore,
  SQLite, files, logs, screenshots, crash reports, or exported diagnostics
- use Android Keystore-backed storage for Android secrets when practical
- exclude all token stores, auth state, pairing state, private keys, and
  recovery material from Android Auto Backup, cloud backup, and device transfer
- add explicit backup exclusion rules for the exact file or directory that
  stores credentials
- verify backup rules with a guard test or grep-style dev check
- do not rely on "app private storage" as the only protection for long-lived
  tokens
- logout must clear every credential store and any in-memory session cache
- token migrations must fail closed if encrypted storage setup fails

Dangerous pattern:

```text
Preferences DataStore contains refresh_token
+
Android allowBackup=true
+
no backup exclusion
```

This can turn a cloud-account compromise into NAS account compromise.

Preferred pattern:

```text
Android Keystore-backed token storage
+
explicit backup/device-transfer exclusions
+
debug verification that credential files are absent from backup artifacts
```

---

## 37. Mobile transport security must fail closed

Mobile clients must not silently downgrade NAS traffic to cleartext.

Rules:

- require `https://` for configured NAS base URLs unless a documented local
  development build explicitly opts out
- reject pairing QR payloads whose origin is not `https://`
- configure Android network security policy to deny cleartext traffic by default
- do not allow QR data to silently override a user-entered or previously trusted
  server origin
- prefer certificate pinning or trust-on-first-use certificate fingerprint
  binding for self-hosted NAS deployments
- surface certificate or origin mismatches as a security warning, not as a
  generic network error
- never send access tokens, refresh tokens, pairing tokens, recovery material,
  file content, or metadata over cleartext HTTP

For self-hosted NAS deployments, the expected pattern is:

```text
initial pairing establishes or confirms the server origin
+
the TLS certificate/fingerprint is pinned or explicitly trusted
+
future API clients enforce that binding
```

---

## 38. QR pairing must bind token, origin, and device intent

QR pairing is a security boundary, not just a convenience flow.

Rules:

- parse QR payloads with a strict scheme, host, required fields, and length
  limits
- require the QR origin to match the configured server origin, or require a
  clear explicit confirmation before accepting a mismatch
- do not let attacker-controlled QR data replace trusted local configuration
  without user-visible security friction
- pair tokens must be high entropy, short-lived, single-use, and server-side
  bound to the intended account/admin action
- pairing requests should include device identity metadata only after the
  origin has been validated
- logs must never include full pairing tokens; prefixes are allowed only in
  debug builds when the token has sufficient entropy and the log is clearly
  non-production
- audit pairing success, failure, expiration, reuse, and origin mismatch events

Dangerous pattern:

```text
user configured https://my-nas.example
QR says origin=https://attacker.example
client sends pair-token consume request to QR origin
```

Preferred pattern:

```text
configured origin and QR origin must match
or
user must explicitly approve a clearly highlighted mismatch
```

---

## 39. Release mobile builds must be hardened

Release builds must not behave like debug builds.

Rules:

- enable R8/ProGuard minification for Android release builds unless a specific
  technical reason is documented
- enable resource shrinking when compatible
- keep required serialization/Retrofit/Moshi DTO rules explicit
- remove hardcoded development servers, test endpoints, and sample credentials
  from release defaults
- use `BuildConfig.VERSION_NAME` or the platform equivalent instead of
  hardcoded client version strings
- debug-only interceptors, verbose logs, test banners, and developer shortcuts
  must be gated behind `BuildConfig.DEBUG` or equivalent
- release builds must not contain debug-only trust managers, disabled TLS
  checks, cleartext fallbacks, or permissive network policies

Verification should include a release build smoke test, not only a debug build.

---

## 40. Production logging must be privacy-minimal

Production client logs must not expose user data or operational metadata.

Avoid logging:

- tokens or token prefixes
- pairing tokens or pairing-token prefixes
- Authorization headers
- full NAS URLs when they include private file paths or query parameters
- private file paths
- workspace names when they reveal private context
- server error bodies that may contain paths, stack traces, SQL details, or
  internal state
- device/user metadata beyond what is required for safe diagnostics

Rules:

- attach HTTP logging interceptors only in debug builds
- prefer structured safe reason codes over raw server messages
- map server errors to user-friendly messages before showing them in UI
- keep technical diagnostics behind explicit debug/export flows
- sanitize logs before crash reporting or support bundles

---

## 41. Mobile file transfer paths must stream large data

Mobile clients must not load unbounded file downloads or uploads fully into
memory.

Rules:

- stream response bodies directly to the selected output stream
- stream uploads from content URIs or bounded staging files
- cap in-memory previews and metadata buffers
- clean temporary staging files on success, cancellation, and error paths
- do not keep sensitive file content in process memory longer than necessary
- use progress reporting that does not require buffering the full file

Dangerous pattern:

```text
val bytes = responseBody.bytes()
outputStream.write(bytes)
```

Preferred pattern:

```text
responseBody.byteStream().copyTo(outputStream)
```

---

## 42. Mobile UI must not block security-sensitive flows on the main thread

Security and auth flows should not use blocking disk or network calls on the UI
thread.

Rules:

- do not use `runBlocking` from Compose/UI event handlers
- use lifecycle-aware coroutine scopes, ViewModels, or suspend functions
- keep token refresh and storage access off the main thread
- avoid deadlocks in HTTP interceptors by keeping token access simple and
  bounded
- fail closed if auth state cannot be loaded safely

Availability bugs in auth flows can become security bugs when users are pushed
toward unsafe retries, screenshots, support dumps, or manual workarounds.

---

## 43. App-level re-authentication for sensitive clients

A paired mobile NAS client should assume that an unlocked phone may still be
temporarily in another person's hands.

Rules:

- provide an app-level lock for sensitive clients
- prefer Android `BiometricPrompt` with a system credential fallback when
  appropriate
- support a configurable lock timeout
- keep the "unlocked this session" state in memory, not as a long-lived
  persisted bypass
- require re-authentication before destructive operations when practical
- logout, token revocation, or device removal must clear local unlocked state

This is defense in depth. It does not replace server-side authorization.


## 44. URL origins must reject display-spoofing components

Security-sensitive origins must be validated as parsed URL components, not as
substrings or display strings.

Rules:

- reject userinfo in origins, for example `https://trusted.example@evil.example`
- reject URL fragments in origins; origins should not contain `#fragment`
- reject leading/trailing whitespace and control characters before parsing
- reject encoded delimiter tricks when they change the parsed authority, host,
  scheme, or path meaning
- normalize scheme and host for comparison, but do not use prefix, suffix, or
  substring matching for trust decisions
- compare canonical origin components: scheme, host, and explicit/default port
- treat trailing-dot hosts, mixed case, explicit ports, and IDNA/punycode as
  test cases, not assumptions
- display the canonical trusted host/origin to the user, not an attacker-chosen
  raw string

Dangerous pattern:

```text
QR origin string is shown as: https://my-nas.example@evil.example
parser host is: evil.example
user sees familiar text and may approve
```

Preferred pattern:

```text
parse URL -> validate components -> canonicalize origin -> compare -> display canonical origin
```

---

## 45. TLS pinning must be mandatory at client factory boundaries

A TLS pinning design is incomplete if any caller can accidentally construct an
unpinned API client.

Rules:

- do not give TLS pin parameters empty defaults such as `tlsPinSha256 = ""`
- do not implement `if pin is blank, skip pinning` in production client factories
- make the pin required by the function signature or type system when the API
  requires a pinned server
- fail during client construction if the pin is missing, malformed, or
  incompatible
- ensure pairing, token refresh, file APIs, thumbnails, media streaming, and
  every authenticated request use the same pinned client factory or equivalent
- keep dev/test cleartext or unpinned clients in explicit debug-only code paths
- add grep/static checks for `CertificatePinner`, pin normalization, and any
  unpinned `OkHttpClient.Builder()` construction

Review question:

```text
Can a future developer call this factory without a pin and still get a working client?
```

If the answer is yes, the API is unsafe by default.

---

## 46. Logout and device removal must revoke server-side tokens

Local credential deletion is necessary but not sufficient for long-lived refresh
tokens.

Rules:

- logout should best-effort revoke the current refresh token on the server, then
  always clear local state
- device removal/revoke flows should invalidate every server-side refresh token
  bound to that device
- local wipe must still happen if the revoke request fails because the NAS is
  offline or unreachable
- token revoke endpoints must require the expected token/device binding and must
  be rate-limited
- revoked refresh tokens must not be accepted for session resurrection
- audit token revoke success, failure, device mismatch, and repeated attempts
- client and server changes must be coordinated; adding only the mobile API call
  is not a complete fix if the server endpoint does not exist

Preferred logout sequence:

```text
load current auth state
if refresh token + device_id + trusted origin + TLS pin exist:
    call pinned revoke endpoint best-effort
clear local token store and in-memory unlocked state regardless of network result
return to unpaired/login state
```

---

## 47. Mobile security fixes need adversarial tests and verified builds

A mobile security patch is not complete until the bypass corpus and the affected
build variant have been tested.

Rules:

- add parser tests for malicious origins such as userinfo, fragments,
  subdomain tricks, mixed case, whitespace, encoded delimiters, trailing dots,
  explicit ports, HTTP, missing pins, and malformed pins
- add pin-normalization tests for every accepted and rejected pin format
- add logout/revoke tests or mocks that prove local state is cleared even when
  the server revoke request fails
- run the relevant unit tests and at least one Android build variant after the
  patch
- if the current machine cannot build because the Android SDK or toolchain is
  missing, report the patch as unverified instead of claiming it compiles
- keep red-team patches small enough that each finding maps to code and tests

This rule exists because AI-assisted patches can look correct while still being
uncompiled, untested, or incompatible with the server.

---

## 48. Server-side red-team audit lessons

Full-repository red-team reviews should be converted into durable engineering rules.
Do not treat a red-team report as only a one-time bug list. If a finding exposes a
repeatable bug class, add a guardrail here, in tests, or in tooling.

### Native binary and crypto helper rules

Security-sensitive native binaries, crypto helpers, WASM modules, and vendored
libraries must be auditable and reproducible.

Rules:

- do not load closed-source or unexplained native binaries into the server process
- do not keep multiple divergent copies of the same security-sensitive binary
- document source, build inputs, version, and expected checksum
- verify checksums before loading optional native helpers when practical
- keep exported symbols minimal for security-sensitive helper libraries
- avoid environment-variable overrides that allow arbitrary shared-library loading
  unless the override is development-only, disabled in production, and explicitly
  documented
- prefer source-built, reproducible, minimal-purpose helpers over broad binary blobs
  with unrelated networking, wallet, or transaction functionality

### Debug, audit, and diagnostic endpoint rules

Debug and audit endpoints are security-sensitive even when they look read-only.

Rules:

- no debug endpoint may expose session IDs, pair tokens, fingerprints, audit history,
  filesystem paths, operational state, or pending authentication state without
  explicit authorization
- production builds should not expose temporary debug routes
- admin-only diagnostic routes must use the same admin authorization helpers as
  normal admin APIs
- audit logs may contain sensitive operational metadata, but normal users and
  unauthenticated callers must never receive raw audit-log contents
- every new diagnostic endpoint must have an explicit owner, auth rule, rate limit,
  and removal/production decision

### Server-side plan and command execution rules

Clients may request an outcome, but the server must own privileged execution plans.

Rules:

- never execute client-supplied shell command arrays, install plans, storage plans,
  restore plans, or helper command strings
- regenerate privileged commands server-side from validated high-level parameters
- keep allowlists for device names, mountpoints, service names, tools, and operations
- remove or hard-disable legacy execute endpoints once safer replacements exist
- if a plan has a dry-run representation, the apply step must verify that the plan
  still matches the validated server-side plan

### Streaming quota and size enforcement

Security checks based only on `Content-Length` are not sufficient for streaming
uploads or generated downloads.

Rules:

- count actual bytes read/written in every streaming upload path
- abort as soon as actual bytes exceed declared length, configured max size, or user
  quota
- apply per-request and per-user limits before and during streaming
- do not trust missing, malformed, or suspicious `Content-Length` values
- generated archives and zip downloads should stream output rather than buffering the
  full artifact in memory
- version blobs, trash, preview caches, audit logs, and generated exports need
  pruning, caps, or retention policies

### HTTP response header and browser hardening rules

Every HTML and file-serving route must set safe browser-facing headers.

Rules:

- add a Content-Security-Policy for HTML responses
- deny framing with `frame-ancestors 'none'` and/or `X-Frame-Options: DENY`
- add `Referrer-Policy` to prevent leaking private paths in outbound navigation
- add HSTS when the deployment is HTTPS-only and the proxy topology supports it
- sanitize `Content-Disposition` filenames before placing them in headers
- reject or encode quotes, backslashes, CR, LF, and control characters in header
  values
- use RFC 5987 / `filename*` encoding for non-ASCII download names when needed
- never construct response headers by concatenating raw filenames or user strings

### Consume, pairing, and device-binding rules

Authentication consume endpoints must bind the secret to the intended browser,
device, session, and account state.

Rules:

- QR/browser consume flows should use a pre-auth browser cookie or equivalent binding
- mobile/app consume flows should bind to the intended device and, where practical,
  a device-held key or signed challenge
- one-time consume state changes must be atomic: pop-or-fail, not get-then-pop
- pair tokens, setup links, reset links, and approval IDs must be single-use,
  short-lived, and invalidated on account/device state changes
- status polling identifiers should not be derivable by QR viewers unless that is an
  explicit, documented design goal
- missing browser/device binding must be treated as a session-stealing risk

### Constant-time comparison and randomness rules

Secret comparisons and security-adjacent identifiers must not use convenience APIs.

Rules:

- compare tokens, MACs, proof bindings, request bindings, and fingerprints with
  constant-time comparison helpers when the value is secret or security-sensitive
- do not use `strcmp()`, `==`, or early-return loops for secret equality checks
- do not use `std::rand()`, `rand()`, or unseeded PRNGs for job IDs, tokens,
  workspace IDs, share IDs, or security-adjacent identifiers
- avoid `mt19937` for identifiers that gate access, uniqueness, privacy, or audit
  correlation
- use approved CSPRNG helpers and document entropy size for new token types
- wipe key material with a function that the compiler cannot optimize away

### Privilege boundary, sudoers, and restore-helper rules

A compromise of the `pqnas` service user must not become trivial root compromise.

Rules:

- the server must not run as root in normal operation
- sudoers entries for helper tools must be narrow, argument-constrained, and reviewed
- avoid broad wildcard sudo rules for tools such as `systemctl`, `smartctl`, `btrfs`,
  restore helpers, and install helpers
- root helpers must canonicalize paths with symlink-aware checks before operating
- root helpers must not trust JSON fields written by the service user without
  validation and allowlists
- restore/delete/chown/chmod helpers must fail closed on malformed paths
- systemd hardening should be applied by default where compatible with current
  functionality

### SQLite and metadata query rules

Parameterized SQL prevents SQL injection, but it does not automatically make query
semantics safe.

Rules:

- set `PRAGMA busy_timeout` or equivalent retry policy for SQLite databases that can
  receive concurrent writes
- protect shared SQLite connections with a mutex or use one connection per thread
  with documented ownership rules
- open security-sensitive metadata databases with restrictive permissions
- escape `%`, `_`, and `\\` when user input is used in `LIKE` patterns, and use an
  explicit `ESCAPE` clause
- treat metadata indexes as security-sensitive when they influence file visibility,
  ownership, sharing, media serving, or quota decisions

### Fail-closed parsing and expiration rules

Malformed security state must not extend access.

Rules:

- expiration parse errors must be treated as expired/invalid, not still valid
- malformed cookies, tokens, grants, share metadata, invite metadata, and approval
  records must fail closed
- user-facing errors should stay generic, while audit logs may record structured
  reason codes
- repair tools may exist, but the runtime path must not silently accept malformed
  authorization state

### Resource-exhaustion and concurrency rules

Every externally reachable path needs a denial-of-service story.

Rules:

- bound pending authentication/session maps and prune them proactively
- set read, write, and idle timeouts for HTTP clients and servers
- cap external process input size, runtime, and output size
- serialize conflicting file operations with path locks, including workspace routes
- avoid recursive full-directory scans on hot upload paths unless bounded or cached
- cache quota/accounting carefully, and invalidate or reconcile it on mutations
- long-running preview, archive, restore, import, and indexing tasks need concurrency
  limits and cancellation/timeout behavior


---

## 49. Signed update packages and release trust

Update package hashes and install plan hashes are integrity controls, not authenticity controls.

A hash can prove that a package did not change after planning, but it does not prove that the package was produced by the DNA-Nexus release process.

Rules:

- core/server binary updates must require a valid signed update manifest
- unsigned, missing-signature, malformed-signature, and wrong-key packages must fail closed before any file modification
- plan hash and package SHA256 must not be treated as release authenticity proof
- release private keys must never be stored in the repository, release tarball, installer assets, logs, screenshots, support bundles, or CI artifacts
- only public release trust anchors may be shipped with DNA-Nexus
- fresh installs must install trusted public update keys automatically
- trusted public keys used by root helpers must be root-owned and not group/world writable
- root update helpers must not read trust-anchor paths or verifier binary paths from caller-controlled environment variables
- the signed manifest must bind update action type, source path, target path, file hash, and intended install mode where practical
- the root helper must verify the signed manifest before preparing backups, chmod/chown changes, binary replacement, or service restart decisions
- signed update verification must be part of both dry-run and apply validation
- dry-run and apply must share the same security validation path
- UI upload/build-plan flow may stay simple, but privileged apply decisions must be owned by the root helper

Required tests:

- correct release key → core update dry-run/apply is accepted
- wrong release key → core update dry-run/apply is rejected before changes
- missing signature → core update dry-run/apply is rejected before changes
- tampered core binary → core update dry-run/apply is rejected before changes

Security rationale:

- `package_sha256 + plan_hash` means integrity after planning.
- `signed manifest + trusted public key` means release authenticity.

Both are required for core binary updates.

---

## 50. Release-key handling and trust-anchor bootstrap

Release signing keys are production secrets.

Rules:

- generate release private keys outside the repository
- keep private keys on a controlled release machine or approved secret store
- never copy private keys into `/tmp`, release staging directories, installer payloads, GitHub release assets, or support bundles
- ship only the public key
- install public keys to a root-controlled trust directory, for example `/etc/pqnas/update-trust.d/`
- public keys may be world-readable, but must not be writable by the service user, group, or other users
- key rotation must support at least one overlap period where old and new public keys are trusted if existing installations need to update safely
- key revocation or emergency rotation must be documented before production use
- development/test keys must be clearly named and must not be accepted by production trust directories

Fresh-install rule:

- tarball contains public trust anchor.
- installer copies it to a root-owned trust directory.
- future Update Center core updates verify against that trust anchor.

Existing-install rule:

- an already installed system cannot verify signed core updates until the trusted public key has been installed through a safe bootstrap path.

---

## 51. Root helper environment and command boundaries

Root helpers are security boundaries.

Rules:

- do not trust caller-controlled environment variables for security decisions
- do not accept helper binary paths, verifier paths, trust-anchor paths, or policy paths from the unprivileged service environment unless explicitly intended for development and disabled in production
- use fixed absolute paths for security-critical helper dependencies where practical
- if configurability is required, load configuration from root-owned, non-writable files
- validate root-owned config file ownership and mode before trusting it
- execute verifier tools with fixed argv and `shell=False` or equivalent
- root helpers must validate every argument using allowlists
- root helpers must reject `/`, `\`, `..`, empty values, unexpected characters, and unsupported modes in identifiers such as plan IDs
- root helpers must validate package source paths, target paths, and action types again even if the server already built a plan

Security comment standard:

- Every new root-helper check should include a short comment explaining what attack path it blocks.

Example:

- Security: service-writable plan+package data must not be enough to replace the core server binary as root.

---

## 52. Archive update extraction must be explicit and fail closed

Archive extraction for updates must never rely on broad convenience extraction.

Rules:

- do not use unsafe full-archive extraction helpers for update packages
- iterate archive members explicitly
- reject absolute paths
- reject `..` traversal
- reject empty member names
- reject symlinks, hardlinks, devices, FIFOs, sockets, and unsupported types
- create directories deliberately
- copy regular files explicitly
- verify the resolved extraction target remains under the extraction root
- normalize package paths consistently before comparing them to manifest entries
- treat top-level package prefixes, such as `pqnas/`, as an explicit normalization case, not as an accident
- bind normalized source paths to signed manifest entries before applying root changes

Dangerous pattern:

- extract all archive members
- inspect extracted result only afterwards

Preferred pattern:

- for each member, normalize the member path
- reject unsafe member type/path
- resolve the target under the extraction root
- copy only regular files or create directories

---

## 53. Security-relevant UI errors must preserve bounded detail

Admin-facing security workflows need actionable error details.

Rules:

- helper output must be parsed as structured JSON when possible
- user-facing admin UI may show bounded `error_detail` for security-relevant failures
- truncate or bound technical details before returning them to the browser
- escape all helper-provided detail before rendering
- do not expose raw command lines, full stdout/stderr dumps, secrets, tokens, or private filesystem paths
- generic user-facing messages are appropriate for normal users, but admin security workflows should distinguish cases such as signature failure, missing trust anchor, bad package hash, and invalid plan hash
- browser cache-busting is required when security-related static JavaScript changes affect admin workflows

Example admin-safe detail:

- `signed update manifest verification failed: Signature Verification Failure`

Unsafe detail:

- raw helper command line, full stderr, private paths, and environment values

---

## 54. SAST triage must separate first-party, bundled, vendor, and test code

Full-repository scans are useful only when the results are triaged by ownership.

Rules:

- classify SAST findings into first-party runtime server code, first-party bundled app code, vendored third-party code, static test vectors, and generated/minified assets
- do not suppress first-party findings just to reduce counts
- fix first-party findings or add narrow line-level justification
- exclude vendored upstream code only after documenting why it is not first-party runtime code
- keep `.semgrepignore` comments close to the ignored paths
- static JWTs, tokens, and recovery-looking strings must be clearly isolated as test vectors before ignoring them
- security-relevant permission warnings must not be blindly changed to `0644`
- executable helpers and binaries may need `0755`
- service runtime directories may need `0750`
- private identity directories may need `0700`
- each permission suppression must explain why the permission is required and why write access is not granted to the wrong principal

Preferred Semgrep workflow:

1. focused scan on touched files
2. fix or justify every touched-file finding
3. run a full scan
4. bucket findings by ownership
5. exclude reviewed vendor/test assets
6. leave first-party findings visible until fixed or justified

---

## 55. Security changes must include positive and negative tests

A security patch is incomplete if it only proves the happy path.

Rules:

- every trust-boundary fix should have at least one positive test and one negative test
- the negative test must prove the old attack path is blocked
- tests should verify that failure happens before mutation
- dry-run paths should be tested separately from apply paths when they are security gates
- UI tests should verify that admins can see enough bounded detail to understand the failure
- release/security tests should avoid using production private keys unless the test is part of the controlled release process

For signed updates:

- positive: correctly signed package is accepted
- negative: wrong-key package is rejected before file changes
- negative: tampered package is rejected before file changes
- negative: missing trust anchor is rejected before file changes

For privileged helpers:

- positive: allowed action succeeds
- negative: unsupported action is rejected
- negative: malformed path is rejected
- negative: traversal/symlink/device input is rejected

---

## 56. Do not let a clean hash hide an untrusted writer

Any state written by a less-privileged user and later consumed by a more privileged helper must be treated as attacker-controlled.

Rules:

- root helpers must not trust JSON plans merely because they are well-formed
- root helpers must not trust package paths merely because they are inside a service-owned directory
- root helpers must revalidate hashes, paths, targets, action types, and ownership boundaries immediately before use
- if the service user can write both the plan and the package, an additional authenticity control is required for privileged actions
- a hash stored next to the object it hashes is not an authority unless it is protected by a stronger trust boundary or signature

Review question:

- If the service user is compromised, can it write every input that the root helper uses to make this decision?

If the answer is yes, add a stronger trust boundary.

## Summary

New DNA-Nexus / PQ-NAS code should be written with these assumptions:

- every external input is hostile
- every external process call is risky
- every missing bound can become a denial-of-service issue
- every debug endpoint can become an information leak
- every installer omission becomes a production misconfiguration
- every security fix should be small, testable, and repeatable
- every one-time secret should have the shortest practical lifetime
- every one-time token should follow account state changes and replacement flows
- every shared security-sensitive state file needs shared locking
- every multi-step security flow should be owned by the backend when partial
  failure would be unsafe
- every browser crypto feature should fail closed instead of falling back
- every security-sensitive URL origin should be parsed, canonicalized, and checked for display-spoofing components
- every TLS-pinned mobile client factory should make missing pins impossible or fail closed
- every logout/device-removal flow should revoke server-side refresh tokens when possible and always clear local state
- every mobile security patch should include adversarial tests and an explicitly verified build status
- every mobile client token store should be encrypted when practical and excluded from backup
- every mobile NAS connection should enforce HTTPS or an explicitly trusted TLS binding
- every QR pairing flow should bind token, origin, and device intent
- every release mobile build should remove debug logging, debug servers, and cleartext fallbacks
- every mobile file transfer path should stream large content instead of buffering whole files
- every multi-file state change needs rollback, repair, or fail-closed behavior
- every native security helper should have source, provenance, and checksum discipline
- every debug or audit endpoint should be explicitly authorized or absent from production
- every privileged plan should be generated and validated server-side, not executed from client-supplied commands
- every streaming upload/download should enforce limits on actual bytes, not only declared headers
- every browser-facing response should have safe headers and sanitized header values
- every consume/pairing flow should bind secrets to the intended browser, device, and account state
- every secret comparison and security-adjacent identifier should use constant-time comparison and CSPRNG-backed generation
- every root helper and sudoers rule should preserve a narrow privilege boundary
- every metadata query should consider wildcard semantics, locking, busy timeouts, and file permissions
- every malformed expiration or authorization record should fail closed
- every externally reachable route should have resource caps, timeouts, and concurrency controls
- every core/server update should prove both integrity and release authenticity
- every root helper should treat service-writable plans and packages as untrusted input
- every security fix should include a negative test that proves the old bypass is blocked
- every SAST suppression should identify whether the code is first-party, bundled, vendor, generated, or test data

The baseline rule is simple:

Do not rely on shell quoting, UI hiding, manual setup, path strings, timing differences, framework cleanup, or developer memory as security controls.

## Runtime dynamic library loading

Server runtime code must not choose dynamically loaded libraries from request
data, user-controlled config, environment variables, database values, or other
deployment-controlled strings.

Do not build `dlopen()` paths from:

- HTTP request parameters
- JSON/config values that can be changed by users or admins through the UI
- database values
- CLI arguments in server runtime paths
- environment variables such as `PQNAS_DNA_LIB`

Allowed patterns:

- Use a fixed absolute path under a root-managed install location.
- Use a small hardcoded allowlist of fixed absolute paths when multiple runtime
  layouts are required.
- For dev/test-only tools, any fallback path must be checked before `dlopen()`:
  regular file, expected location, and expected cryptographic hash.

Security reason: `dlopen()` loads executable code into the server process.
Letting an environment variable, request value, or writable config choose the
library path can turn a bad deployment setting, environment injection, or path
manipulation bug into arbitrary code execution.

## Authentication state file paths

Authentication state files such as `opaque_enrollments.json` and
`password_enrollments.json` must use one shared, deterministic path derivation
across every writer.

Do not redirect authentication/enrollment stores with environment variables such
as `PQNAS_OPAQUE_ENROLLMENTS_PATH` or `PQNAS_PASSWORD_ENROLLMENTS_PATH`.

Allowed pattern:

- derive the store path from the same trusted server config path in every writer
- keep revoke, onboarding, workspace invite, and login flows on the same file
- use the existing lock/atomic-write discipline for shared auth state files

Security reason: if different server paths can be selected by environment or
deployment-controlled values, auth flows can split across different token stores.
That can create stale enrollment tokens, missed revoke invalidation, or path
manipulation risks around authentication state.

## Secret-bearing configuration file paths

Configuration files that store secrets must not have per-file environment
variable overrides.

Examples include notification settings that contain Telegram bot tokens, SMTP
usernames, SMTP passwords, API tokens, or similar credentials.

Do not add environment variables such as `PQNAS_NOTIFICATIONS_PATH` that redirect
one secret-bearing settings file to an arbitrary runtime path.

Allowed pattern:

- derive the path from a trusted server config root
- use a fixed filename for the settings file
- keep file permissions restrictive, for example `0600`
- use atomic temp-file plus rename writes for updates

Security reason: per-file path overrides make it easier for bad deployment
settings, environment injection, or path manipulation bugs to redirect secrets
to the wrong location or read/write an unintended file.

## Federation identity paths

Federation identity directories must not be redirected with per-feature
environment variables.

Do not add environment variables such as `PQNAS_NODUS_IDENTITY_DIR` that choose
which Nodus identity directory the server uses at runtime.

Allowed pattern:

- use the production root-managed identity directory
- use an explicit hardcoded legacy fallback only when migrating old layouts
- keep worker, admin, research, and Circle Stack routes on the same deterministic path
- read only the expected identity files such as `nodus.fp`

Security reason: the Nodus identity selects the local federation origin.
Redirecting it through environment-controlled paths can create split-brain
federation identity, impersonation-by-misconfiguration, or unintended reads from
the wrong identity directory.

## Storage root paths

Storage-manager code must not read `PQNAS_STORAGE_ROOT` directly in route
handlers, pool helpers, or destructive storage operations.

Allowed pattern:

- read deployment-level `PQNAS_STORAGE_ROOT` only through `pqnas::storage_root_dir()`
  or `pqnas::storage_root_path()`
- require the configured root to be absolute and normalized
- reject empty, relative, or filesystem-root values
- derive `config/pools.json`, `pools/<pool_id>`, and default `data` paths from
  the centralized sanitized storage root
- keep request-provided mount paths behind separate absolute-path and
  allowed-prefix checks

Security reason: the storage root is used as an allow-list boundary for pool
mounts and storage-manager operations. Parsing it in multiple places risks
inconsistent validation, path manipulation, and accidentally broad destructive
operation scopes.

## Authentication credential file paths

Authentication credential stores must not have per-file environment variable
overrides.

Do not add environment variables such as `PQNAS_PASSWORD_CREDENTIALS_PATH` or
`PQNAS_OPAQUE_CREDENTIALS_PATH` that redirect credential stores at runtime.

Allowed pattern:

- derive password credentials beside the active `users.json`
- derive OPAQUE credentials from the trusted runtime config root with the fixed
  filename `opaque_credentials.json`
- keep every login, workspace, invite, and cleanup route on the same helper
- keep credential file writes atomic and permissions restrictive

Security reason: password and OPAQUE credential files are authentication state.
Per-file path overrides can split login, cleanup, migration, and revoke flows
across different stores, leaving stale credentials or authenticating against an
unexpected file.

## Core configuration file paths

Core configuration and authorization files must not have per-file environment
variable overrides.

Do not add environment variables such as `PQNAS_ADMIN_SETTINGS_PATH`,
`PQNAS_POLICY_PATH`, `PQNAS_USERS_PATH`, `PQNAS_APP_AUTH_PATH`, or
`PQNAS_SHARES_PATH` that redirect individual core state files at runtime.

Allowed pattern:

- derive core config files from the trusted runtime config root
- use fixed filenames such as `admin_settings.json`, `policy.json`,
  `users.json`, `app_auth.json`, and `shares.json`
- keep `shares.json` beside the active `users.json`
- keep backup, login, admin, and sharing flows on the same deterministic files
- use explicit repository config fallbacks only for local development

Security reason: these files hold identity, authorization, device auth, sharing,
and admin policy state. Per-file path overrides can split security state across
different files, causing stale access rules, missed revocation, or authentication
against an unexpected registry.
