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

    program: /usr/local/bin/nodus-cli
    argv:
      - /usr/local/bin/nodus-cli
      - -i
      - /srv/pqnas/config/nodus/identity
      - identity-init

Avoid this:

    /bin/sh -c "timeout 5 /usr/local/bin/nodus-cli -i '/srv/pqnas/config/nodus/identity' identity-init 2>&1"

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

    validate path string
    later open/remove/copy path

Safer pattern:

    resolve allowed root
    open/check using filesystem APIs
    verify the final target is still inside the allowed root
    avoid following symlinks unless explicitly intended

---

## 4. Avoid TOCTOU bugs

TOCTOU means "time of check to time of use".

A bug can happen when code checks a path or state, then later uses it after an attacker or another process has changed it.

Dangerous pattern:

    if file exists and is safe:
        open file later

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

---

## 7. Rate-limit sensitive and expensive endpoints

Add rate limits to endpoints that are:

- login or verification related
- token/session related
- upload related
- update/install related
- federation related
- media-preview related
- CPU-heavy
- disk-heavy
- network-heavy
- capable of spawning external processes

Rate-limit keys should normally include client IP and route/action name.

Rate-limited responses should return HTTP 429 and a safe generic error.

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
- debug dumps containing credentials

Pre-commit scanning is a guardrail, not a replacement for care.

The scanner should block real secrets without blocking normal feature flags.

---

## 10. Environment and key files must have strict permissions

Environment files that may contain secrets or security-sensitive configuration should be installed with restrictive permissions.

Recommended default:

    owner: root
    group: root
    mode: 0600

Do not print env file contents during debugging.

Use `stat` or `ls -l` to verify permissions without exposing values.

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

Fresh installs must work securely without undocumented manual edits.

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
- raw Authorization headers
- full paths to private files
- raw external command output

When logging identifiers, prefer shortened or hashed forms if the full value is not required.

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

Unbounded tables become a denial-of-service risk.

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

## 25. Default decision rule

When uncertain, choose the safer default:

- deny instead of allow
- private instead of public
- bounded instead of unbounded
- configured instead of hardcoded
- argv execution instead of shell execution
- explicit authorization instead of implicit trust
- sanitized summary instead of raw technical detail
- installer-backed default instead of manual setup

---

## Summary

New DNA-Nexus / PQ-NAS code should be written with these assumptions:

- every external input is hostile
- every external process call is risky
- every missing bound can become a denial-of-service issue
- every debug endpoint can become an information leak
- every installer omission becomes a production misconfiguration
- every security fix should be small, testable, and repeatable

The baseline rule is simple:

Do not rely on shell quoting, UI hiding, manual setup, path strings, or developer memory as security controls.
