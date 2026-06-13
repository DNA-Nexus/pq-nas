#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("docs/dna_nexus_pqnas_auth_methods.md")
if not p.exists():
    print(f"ERROR: missing {p}", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

old = """Current status:

```text
scaffold only
```

The routes exist as fail-closed placeholders until the reviewed OPAQUE crypto backend and browser-side client are integrated.
"""

new = """Current status:

    server-side OPAQUE login and browser-side OPAQUE client integration are implemented

The current implementation includes:

- server-side OPAQUE registration/enrollment handling
- helper-backed OPAQUE login transcript handling
- browser-side OPAQUE client module integration
- OPAQUE login UI in browser login mode
- `pqnas_session` minting after successful OPAQUE transcript verification
- enabled-user check before session minting
- `/api/v4/me` verification before redirecting to `/app`

Production readiness still depends on keeping the selected helper version, browser client, protocol suite, serialization format, and stored credential format tested together.

The login UI must continue to fail closed if the browser OPAQUE module is missing, incompatible, or not loaded.
"""

if old not in s:
    print("ERROR: expected old OPAQUE status block not found", file=sys.stderr)
    sys.exit(1)

s = s.replace(old, new, 1)

insert_after = """OPAQUE browser login must not replace `AppTokenStore`, trusted device records, refresh tokens, or mobile bearer-token verification.

"""

add = """## Browser OPAQUE client rule

OPAQUE mode uses a browser-side OPAQUE client.

Rules:

- the browser performs OPAQUE client cryptographic steps locally
- the browser must never send plaintext password fields to OPAQUE endpoints
- the OPAQUE browser module must fail closed if missing or incompatible
- OPAQUE mode must not silently call `/api/auth/password/login`
- the browser may redirect to `/app` only after `/api/v4/me` confirms that the standard `pqnas_session` works

Server-side OPAQUE readiness and browser-side OPAQUE client compatibility must be tested together before treating a build as production-ready.
"""

if add.strip() not in s:
    if insert_after not in s:
        print("ERROR: mobile model anchor not found", file=sys.stderr)
        sys.exit(1)
    s = s.replace(insert_after, insert_after + add, 1)

s = s.replace(
    "If `PQNAS_LOGIN_MODE=opaque` is selected but the OPAQUE backend is not fully wired:",
    "If `PQNAS_LOGIN_MODE=opaque` is selected but the OPAQUE backend or browser-side OPAQUE client is not fully wired:"
)

p.write_text(s)
print("updated docs/dna_nexus_pqnas_auth_methods.md")
