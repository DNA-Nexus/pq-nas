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
- every multi-file state change needs rollback, repair, or fail-closed behavior

The baseline rule is simple:

Do not rely on shell quoting, UI hiding, manual setup, path strings, timing differences, framework cleanup, or developer memory as security controls.