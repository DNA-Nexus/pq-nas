#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("docs/dna_nexus_pqnas_auth_methods.md")
if not p.exists():
    print(f"ERROR: missing {p}", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

status_start = s.find("Current status:\n")
shared_marker = "\n## Shared success result\n"
status_end = s.find(shared_marker, status_start)

if status_start == -1 or status_end == -1:
    print("ERROR: could not find Current status block or Shared success result marker", file=sys.stderr)
    sys.exit(1)

new_status = """Current status:

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
"""

s = s[:status_start] + new_status + s[status_end:]

fail_marker = "\n## Fail-closed rule\n"

client_section = """## Browser OPAQUE client rule

OPAQUE mode uses a browser-side OPAQUE client.

Rules:

- the browser performs OPAQUE client cryptographic steps locally
- the browser must never send plaintext password fields to OPAQUE endpoints
- the OPAQUE browser module must fail closed if missing or incompatible
- OPAQUE mode must not silently call /api/auth/password/login
- the browser may redirect to /app only after /api/v4/me confirms that the standard pqnas_session works

Server-side OPAQUE readiness and browser-side OPAQUE client compatibility must be tested together before treating a build as production-ready.

"""

# Replace the earlier bad inserted section if it exists.
for heading in [
    "\n## Browser OPAQUE client requirement\n",
    "\n## Browser OPAQUE client rule\n",
]:
    start = s.find(heading)
    if start != -1:
        end = s.find(fail_marker, start)
        if end == -1:
            print("ERROR: browser client section found but Fail-closed rule marker missing", file=sys.stderr)
            sys.exit(1)
        s = s[:start + 1] + client_section + s[end:]
        break
else:
    insert_after = "OPAQUE browser login must not replace `AppTokenStore`, trusted device records, refresh tokens, or mobile bearer-token verification.\n\n"
    if insert_after not in s:
        print("ERROR: mobile model anchor not found", file=sys.stderr)
        sys.exit(1)
    s = s.replace(insert_after, insert_after + client_section, 1)

s = s.replace(
    "If `PQNAS_LOGIN_MODE=opaque` is selected but the OPAQUE backend is not fully wired:",
    "If `PQNAS_LOGIN_MODE=opaque` is selected but the OPAQUE backend or browser-side OPAQUE client is not fully wired:"
)

# Remove the exact bad phrase if it survived somewhere.
s = s.replace(
    "server-side OPAQUE support exists; browser client still pending",
    "server-side OPAQUE login and browser-side OPAQUE client integration are implemented"
)

p.write_text(s)
print("fixed OPAQUE auth methods status")
