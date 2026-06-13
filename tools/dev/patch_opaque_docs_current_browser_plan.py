#!/usr/bin/env python3
from pathlib import Path
import sys

browser = Path("docs/technical/opaque_browser_client_plan.md")
design = Path("docs/technical/opaque_login_design.md")

for p in (browser, design):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

# ---------------------------------------------------------------------
# docs/technical/opaque_browser_client_plan.md
# ---------------------------------------------------------------------
s = browser.read_text()

s = s.replace(
"""Current backend status: OPAQUE registration, helper login transcript, and session minting are implemented server-side.""",
"""Current backend status: server-side OPAQUE registration/enrollment, helper login transcript handling, and `pqnas_session` minting paths exist. Production browser OPAQUE login is not enabled until a compatible browser-side OPAQUE client is implemented and tested."""
)

preferred_anchor = """The API should not expose unrelated helper operations.

"""

compat_section = """## Required protocol compatibility decisions

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

"""

if compat_section.strip() not in s:
    s = s.replace(preferred_anchor, preferred_anchor + compat_section, 1)

ui_anchor = """## UI requirements

"""

loading_section = """## Browser module loading policy

The OPAQUE browser module must be served as a local versioned static asset.

Rules:

- do not load OPAQUE crypto from a CDN
- do not dynamically select an unpinned external package at runtime
- fail closed if the WASM module or JS wrapper is missing
- fail closed if the browser module version is incompatible with the server-supported OPAQUE suite
- show a safe unavailable state instead of falling back to password login
- never call `/api/auth/password/login` while `mode=opaque`

The UI should treat module initialization failure as an authentication-method-unavailable state, not as a password-login opportunity.

"""

if loading_section.strip() not in s:
    s = s.replace(ui_anchor, loading_section + ui_anchor, 1)

secrets_anchor = """The client login state must be scoped to one login attempt and discarded after success or failure.

"""

hardening_section = """## Client flow hardening

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

"""

if hardening_section.strip() not in s:
    s = s.replace(secrets_anchor, secrets_anchor + hardening_section, 1)

failure_anchor = """This avoids account enumeration.

"""

failure_extra = """This rule applies to both `login/start` and `login/finish`. When practical, `login/start` should use a dummy/missing-user flow or otherwise preserve a response shape that does not reveal whether the login exists.

"""

if failure_extra.strip() not in s:
    s = s.replace(failure_anchor, failure_anchor + failure_extra, 1)

browser.write_text(s)

# ---------------------------------------------------------------------
# docs/technical/opaque_login_design.md
# ---------------------------------------------------------------------
s = design.read_text()

start = s.find("Current implementation status:\n")
end = s.find("\nThe current OPAQUE scaffold must not be considered", start)

if start == -1 or end == -1:
    print("ERROR: could not find Current implementation status block", file=sys.stderr)
    sys.exit(1)

new_status = """Current implementation status:

- `PQNAS_LOGIN_MODE=opaque` is recognized as a third browser login mode.
- `/api/auth/config` can report OPAQUE mode.
- The login UI must not silently fall back to classic password login in OPAQUE mode.
- `pqnas_opaque_helper` exists as the Rust helper boundary using `opaque-ke`.
- The helper supports server setup validation, registration operations, and login transcript operations.
- `OpaqueCredentials` exists as the C++ storage/parsing layer for `opaque_credentials.json`.
- OPAQUE runtime path helpers exist for credentials, server setup, helper binary, and enrollment-token storage.
- `OpaqueBackendStatus` exposes admin-only diagnostics; public OPAQUE login errors remain generic.
- Admin-side OPAQUE registration/enrollment and reset-token flows exist.
- Public OPAQUE login start/finish routes exist server-side.
- `POST /api/auth/opaque/login/finish` can mint the standard `pqnas_session` after a valid OPAQUE transcript, a valid pending login state, and an enabled user check.
- Existing QR login, classic password login, mobile pairing, and app token logic are intentionally unchanged.
- Browser-side OPAQUE login is still not production-ready until a compatible browser/WASM client is implemented and tested against the same helper version, suite, and serialization.

"""

s = s[:start] + new_status + s[end + 1:]

old_scaffold_line = "The current OPAQUE scaffold must not be considered a working OPAQUE login implementation.\n\n"
replacement = """Document maintenance note:

This document contains both the current design and historical phase notes written while OPAQUE support was being built. The `Current implementation status` section above is the authoritative current-state summary. Older sections that say public OPAQUE login was still fail-closed or did not mint sessions are historical notes unless explicitly repeated in the current-status section.

Browser-side OPAQUE login must still be completed before OPAQUE can be treated as a normal production browser login method.

"""

if old_scaffold_line in s:
    s = s.replace(old_scaffold_line, replacement, 1)

# Soften one clearly stale backup note near the end if present.
s = s.replace(
"""`ready_for_login` must remain `false` until real OPAQUE registration,
login-finish verification, and session minting are intentionally
implemented.
""",
"""`ready_for_login` must reflect the current deployment state. Server-side
registration, login-finish verification, and session minting may exist, but
production browser OPAQUE login still depends on the compatible browser client
being implemented and tested.
"""
)

design.write_text(s)

print("updated OPAQUE browser/client design docs")
