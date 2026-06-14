# Security Model - DNA-Nexus Server / PQ-NAS

This document describes the security model, trust boundaries, and design
decisions of DNA-Nexus Server / PQ-NAS.

DNA-Nexus Server is an identity-first storage and collaboration server. The
internal security anchor is the DNA fingerprint. Login methods are entry paths
into the same fingerprint-backed session and authorization model.

The project was originally called PQ-NAS. Some service names, paths, binaries,
and source identifiers may still use `pqnas` during the transition to the public
DNA-Nexus Server name.

---

## Current Security Contexts

DNA-Nexus Server currently has several distinct security contexts:

1. Browser login and session establishment
2. Mobile trusted-device authentication
3. Authorization and access control
4. Public sharing and Drop Zone upload flows
5. Workspace and external-member access
6. Post-quantum share protection and browser-side file opening
7. Admin, onboarding, reset, and recovery flows
8. Audit logging and security event history

These contexts have different trust assumptions and must not be conflated.

The browser may be an interface, a local OPAQUE client, or a local share
decryption endpoint depending on the feature. It is not automatically a trust
anchor for the whole system.

---

## Threat Model

DNA-Nexus Server is designed under the following assumptions.

An attacker may:

- control or compromise the browser
- steal browser cookies or local storage
- observe or manipulate network traffic
- attempt replay or token substitution attacks
- attempt to authenticate without owning valid credentials or identity keys
- attempt account enumeration through login or recovery flows
- attempt to misuse setup, enrollment, invite, reset, or bootstrap tokens
- attempt to exploit partial multi-file state updates
- attempt to misuse public shares, workspace invites, or Drop Zone links
- attempt to access private files through stale preview or cache state
- attempt to tamper with PQ share ciphertext or wrapped key material
- attempt to exhaust CPU, disk, network, or external process resources
- attempt to abuse federation or remote NAS input

The server must assume that all external input is hostile.

---

## Trusted Components

For QR / DNA Connect browser login:

- the DNA-Nexus Server process
- the user's trusted mobile device
- DNA-Messenger or compatible approval client
- the DNA identity private key stored on the trusted device

For classic password browser login:

- the DNA-Nexus Server process
- the configured password credential store
- the password hashing and verification implementation
- the user's ability to keep their password secret

For OPAQUE browser login:

- the DNA-Nexus Server process
- the server-side OPAQUE helper boundary
- the configured OPAQUE server setup and credential store
- the browser-side OPAQUE client module for local protocol steps
- the user's ability to keep their password secret

For mobile trusted-device access:

- the DNA-Nexus Server process
- the mobile app token store
- issued access and refresh tokens
- trusted device records

For PQ share opening:

- the DNA-Nexus Server process
- the recipient browser runtime performing local unwrap/decrypt
- the recipient browser-side share identity key material used for share opening

This distinction matters. The browser is not a general authentication authority,
but some features intentionally use the browser as a local cryptographic
endpoint.

---

## Core Security Principles

### 1. One internal identity model

The internal identity anchor is the DNA fingerprint.

All successful browser login methods resolve to:

    fingerprint -> pqnas_session

The rest of the server should enforce authorization based on the resolved
fingerprint, role, status, workspace membership, share policy, and app
permissions.

### 2. Login methods are entry paths

Supported browser login methods may include:

- QR / DNA Connect login
- classic password login
- OPAQUE zero-knowledge password login

These methods differ in how the user proves access, but they converge to the
same session and authorization model.

### 3. No silent fallback

A configured login mode must fail closed if it is unavailable.

Examples:

- OPAQUE mode must not silently fall back to classic password login.
- password mode must not accidentally expose QR login if QR is disabled.
- QR mode must not accidentally expose password endpoints as unintended browser
  login paths.

### 4. Authentication does not create users

Login, recovery, or OPAQUE enrollment attempts must never create users.

User creation must be explicit, auditable, and tied to provisioning such as
admin user creation, bootstrap, invitation, approved onboarding, or another
reviewed flow.

### 5. Authorization is explicit

Authentication proves who is trying to access the system.

Authorization decides whether that identity may perform the action.

Every route must define who can call it and must enforce the required role,
status, session, token, workspace membership, or share policy server-side.

### 6. Sensitive state changes must fail closed

Partial state must not grant access.

Security-sensitive multi-step operations should be owned by one backend endpoint
when partial frontend success could leave an unsafe state.

Examples:

- user onboarding
- OPAQUE enrollment
- force reset
- password reset
- invite acceptance
- bootstrap first-admin setup
- workspace external access
- share creation or revocation

### 7. One-time secrets have the shortest practical lifetime

Setup links, enrollment tokens, reset links, bootstrap tokens, invite tokens,
and recovery material are sensitive credentials.

They must be short-lived, purpose-bound, and invalidated when account state
changes.

---

## Browser Login Security Model

### QR / DNA Connect login

QR login is the device-mediated login model.

Typical flow:

1. Browser requests access.
2. Server displays a QR challenge.
3. DNA-Messenger or compatible trusted device scans the challenge.
4. User approves the login on the trusted device.
5. The trusted device signs or proves the challenge using the user's identity.
6. The server verifies the proof and policy.
7. The server mints `pqnas_session`.

The browser is treated as an interface. It does not prove identity by itself.

### Classic password login

Classic password login is a configured browser login mode.

Rules:

- password login must be explicitly configured
- login/email maps to an internal fingerprint
- passwords are stored only as password hashes
- plaintext passwords must not be logged
- missing-user and wrong-password paths should avoid obvious enumeration
- disabled or pending users must not authenticate
- login must never create a user
- password mode must not expose another browser login method accidentally

### OPAQUE zero-knowledge password login

OPAQUE login is a configured browser login mode where the plaintext password is
processed locally by the browser-side OPAQUE client and is not sent to the
server.

Current model:

- browser loads the local OPAQUE client module
- browser performs OPAQUE client protocol steps locally
- server performs helper-backed OPAQUE transcript handling
- server verifies the OPAQUE flow
- server checks user status and policy
- server mints `pqnas_session` after successful verification
- browser verifies the session with the normal user/session endpoint before
  redirecting to the app

Rules:

- do not load authentication crypto from a CDN
- serve browser crypto as local versioned static assets
- fail closed if the browser OPAQUE module is missing or incompatible
- never send plaintext passwords to OPAQUE endpoints
- never silently call the classic password endpoint from OPAQUE mode
- keep helper version, browser client, protocol suite, serialization format, and
  stored credential format tested together
- clear password/client state as soon as practical
- do not store OPAQUE client state in localStorage, sessionStorage, IndexedDB,
  URLs, logs, or DOM attributes

---

## Mobile Trusted-Device Model

DNA-Nexus Mobile uses the trusted-device / bearer-token model.

Mobile pairing and app tokens are separate from browser login mode.

Typical model:

    trusted mobile device -> access token / refresh token -> fingerprint + role + device_id

OPAQUE browser login must not replace:

- AppTokenStore
- trusted device records
- refresh tokens
- mobile bearer-token verification
- mobile pairing policy

---

## Session Security

Browser sessions are represented by signed cookies.

Cookies should default to:

- HttpOnly
- Secure
- SameSite=Strict where practical

Use SameSite=Lax only when a specific browser flow requires it.

Session rules:

- mint sessions only after authentication and authorization succeed
- use short or reasonable lifetimes
- verify cookie parsing strictly
- do not build cookie claims with unsafe string concatenation
- validate claim fields before minting and after parsing
- fail closed on malformed or unexpected claims
- do not log session IDs or cookie values

Session cookies are bearer tokens. A stolen valid session cookie is security
sensitive even if the original login method was strong.

---

## Authorization Model

DNA-Nexus Server separates authentication from authorization.

Authorization is enforced through:

- fingerprint identity
- user status
- user role
- admin role
- workspace membership
- workspace role
- share policy
- Drop Zone policy
- app permissions
- storage quota and pool assignment
- explicit route-specific checks

A cryptographically valid identity may still be denied access.

Disabled, pending, or revoked users must fail closed.

---

## Onboarding, Enrollment, Reset, and Recovery

Onboarding and recovery flows are security-sensitive.

Rules:

- setup and enrollment tokens must be short-lived
- token hashes should be stored instead of plaintext tokens when practical
- new replacement tokens should invalidate older active tokens for the same
  purpose and user
- revoking a user must invalidate active setup/enrollment/reset tokens for that
  user
- tokens should be purpose-bound and user-bound
- plaintext one-time tokens must not be logged
- plaintext one-time tokens must not be returned after partial failure
- backend endpoints should own multi-step reset/enrollment flows
- rollback or repair must be possible after partial state failure
- recovery must not create users
- recovery must not enable disabled or pending users unless a separate explicit
  admin-approved transition is performed

One-time recovery phrases or equivalent secrets should be shown once and should
not be stored server-side.

---

## Shared State and Locking

Security-sensitive JSON state files must use safe read-modify-write behavior.

Rules:

- use atomic temp-file and rename saves
- use unique temporary filenames
- keep lock coverage across load, validate, mutate, and save
- use shared locking when more than one module can write the same file
- do not rely only on a file-scope static mutex if another source file or process
  can update the same persistent file
- document lock ordering when mixing in-process mutexes and file locks
- use rollback or repair for multi-file updates

Examples of security-sensitive state:

- users.json
- password credentials
- OPAQUE credentials
- OPAQUE enrollment tokens
- app tokens
- invite tokens
- bootstrap state
- workspace metadata
- share metadata
- update state

---

## Path, File, and Storage Safety

All paths are dangerous input, even when they appear to come from local state.

Rules:

- normalize paths before use
- enforce allowed roots
- reject path traversal
- reject unexpected absolute paths
- reject or explicitly handle symlinks
- avoid TOCTOU check-then-use sequences
- avoid shell command strings for path-sensitive work
- verify authorization immediately before serving, deleting, moving, restoring,
  previewing, or exporting files
- do not trust cached visibility for later media/file serving

Storage operations must fail closed when ownership, pool, quota, or path policy
cannot be verified.

---

## Public Sharing and Drop Zone

Share links and Drop Zone links are intentional public access paths.

Rules:

- use high-entropy tokens
- store token hashes when practical
- apply expiry, password, destination, role, and size limits
- rate-limit public upload and download endpoints
- validate current share or Drop Zone policy before every access
- do not expose private filesystem paths
- prevent stale preview/cache access after visibility changes
- audit creation, use, revocation, and failure events
- fail closed when link metadata is missing or inconsistent

Drop Zone must remain one-way unless explicitly designed otherwise. Uploaders
must not gain directory browsing access merely because they have an upload link.

---

## Workspaces and External Access

Workspace access must be role-based and explicit.

Rules:

- verify workspace membership on every workspace operation
- verify external member role and status
- never rely on UI hiding for workspace security
- external invites must be high entropy, time-limited where appropriate, and
  bound to the intended workspace
- role changes must take effect immediately
- stale cached media or previews must not bypass current workspace visibility
- audit sensitive workspace membership and external-access changes

---

## Post-Quantum Share Security Model

PQ share opening is a separate security path from login.

For PQ share opening:

- a file content encryption key is wrapped for the intended recipient
- CEK unwrap and file decryption occur locally in the browser
- the server can deliver encrypted payloads without directly exposing plaintext
  in transit

The browser is therefore an active security endpoint for share decryption.

This means:

- no browser-resident authentication identity secret is required for login
- browser-resident share-opening private key material may still exist for PQ
  share opening
- if the browser used to open a PQ share is compromised, locally unwrapped CEK
  material and plaintext may be exposed
- integrity verification may detect tampering, but endpoint compromise still
  compromises confidentiality

This is an accepted limitation of the current local-browser-decrypt share model.

---

## ML-KEM Provider Model

ML-KEM operations should be routed through a DNA-owned wrapper boundary rather
than having application code depend directly on vendored ML-KEM symbols.

Security goals:

- keep a stable internal provider API
- isolate provider choice behind a selector seam
- keep native fallback tested while replacement work continues
- exercise default, forced-DNA, and forced-native lanes when relevant
- document whether vendored native code is still part of the effective security
  path

Vendored native code must not be considered removable until the validation and
provider boundary no longer require it.

---

## Audit and Accountability

Security-relevant events should be recorded in audit logs.

Examples:

- authentication attempts
- session minting
- authorization failures
- admin actions
- onboarding/enrollment/reset token lifecycle events
- workspace membership and external-access changes
- public share and Drop Zone events
- update/install events
- PQ share operations where instrumentation is present
- rollback failures or repair-required states

Audit logs are for incident analysis, forensics, and security review.

User-facing activity is different from audit logging and must not expose raw
security internals.

---

## Logging Rules

Do not log sensitive values.

Avoid logging:

- raw passwords
- OPAQUE secrets or client state
- session IDs
- cookie values
- Authorization headers
- private keys
- recovery phrases
- bootstrap tokens
- invite tokens
- reset tokens
- plaintext one-time setup links
- raw external command output
- full private file paths
- full private fingerprints unless required

User-facing errors should stay generic for authentication and recovery failures.
Audit logs may contain structured internal reason codes, but must not contain
secrets.

---

## Rate Limiting and Resource Bounds

Sensitive and expensive endpoints must be rate-limited.

Examples:

- QR verification
- password login
- OPAQUE login start and finish
  - login/start must have both per-IP+login and global per-login limits
  - login/finish must be limited by pending login id and request source
- token creation and consumption
- bootstrap and first-admin setup
- invite acceptance
- recovery flows
- public upload/download
- media preview generation
- federation ingest
- update/install
- external process execution

Rate limits should normally include both per-IP and account/token-bound limits
where applicable.

In-memory rate-limit maps must have cleanup behavior or bounded size.

External process output must be bounded, truncated safely, and never returned
raw to normal users.

---

## Federation Safety

Federation data is untrusted remote input.

Required controls:

- pre-parse size limits
- strict JSON parsing
- canonical JSON for signatures
- cryptographic signatures for events
- public key and origin identity binding
- verification before storing or applying
- rejection of unsigned or invalid events
- pruning or caps for unbounded tables
- safe media reference handling
- no direct trust in remote-provided paths

Invalid federation data should be rejected and audit logged.

---

## Update Center Safety

Update/install features must fail closed.

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

Installer changes are part of security fixes. A runtime security fix is
incomplete if a fresh install still produces unsafe defaults.

---

## Current Security Posture Summary

At this checkpoint:

- DNA-Nexus Server uses one internal fingerprint-backed identity model.
- QR, password, and OPAQUE browser login modes converge to the same
  `pqnas_session` model.
- QR login remains device-mediated.
- OPAQUE login avoids sending plaintext passwords to the server.
- Classic password login is explicit and configured, not the only identity model.
- Mobile trusted-device bearer tokens remain separate from browser login mode.
- PQ share opening uses local browser unwrap/decrypt and has a distinct endpoint
  trust model.
- ML-KEM operations should remain behind the DNA wrapper/provider boundary.
- Security-sensitive state changes must use atomic write, shared locking,
  rollback, repair, or fail-closed behavior.
- Public sharing, Drop Zone, and workspace external access are intentional but
  tightly scoped public/external access paths.

The architecture is stronger than the early QR-only v0 model, but it should
continue to be reviewed as new login, sharing, federation, and app features are
added.

---

## Non-Goals

The following are not current security claims:

- resistance against a fully compromised server
- a claim that browser-side PQ share decryption is safe under full browser
  compromise
- a claim that browser sessions are harmless if stolen
- a claim that classic password login is equivalent to device-mediated QR login
- a claim that OPAQUE removes the need for rate limits or secure onboarding
- a claim that vendored ML-KEM code has already been completely removed from the
  effective security path
- a claim that UI hiding is a security boundary
- a claim that public share links or Drop Zone links are private once disclosed

---

## Responsible Disclosure

If you discover a security issue:

- do not open a public issue
- contact the project maintainers privately
- provide a minimal reproduction and impact assessment
- avoid sharing exploit details publicly until a fix or mitigation exists

Security issues will be acknowledged and addressed responsibly.

---

## Security Philosophy

> Identity should belong to the user, not the server.
>
> The browser is only the interface.
>
> Login methods are entry paths into a fingerprint-backed authorization model.
>
> Public access must be explicit, scoped, and revocable.
>
> Security-sensitive flows must fail closed.
