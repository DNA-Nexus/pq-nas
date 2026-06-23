#!/usr/bin/env python3
from pathlib import Path
import re
import sys

p = Path("README.md")
if not p.exists():
    print("ERROR: README.md not found", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

def section_bounds(text: str, title: str):
    pat = re.compile(rf"(?m)^#{{1,3}}\s+{re.escape(title)}\s*$")
    m = pat.search(text)
    if not m:
        return None
    next_heading = re.compile(r"(?m)^#{1,3}\s+.+$")
    n = next_heading.search(text, m.end())
    if not n:
        return (m.start(), len(text))
    return (m.start(), n.start())

def replace_section(title: str, new_section: str):
    global s
    b = section_bounds(s, title)
    if not b:
        print(f"WARNING: section not found: {title}", file=sys.stderr)
        return
    start, end = b
    s = s[:start] + new_section.rstrip() + "\n\n" + s[end:].lstrip("\n")

def insert_before_heading(possible_titles, block: str):
    global s
    if block.strip() in s:
        return
    for title in possible_titles:
        b = section_bounds(s, title)
        if b:
            start, _ = b
            s = s[:start] + block.rstrip() + "\n\n" + s[start:]
            return
    print("WARNING: no insertion heading found for Documentation Map; appending near end", file=sys.stderr)
    s = s.rstrip() + "\n\n" + block.rstrip() + "\n"

# Clean common mojibake bullets if present.
s = s.replace(" ? identity", " - identity")
s = s.replace(" ? storage", " - storage")

# Opening description: replace old QR-only paragraph if found.
old_intro = re.compile(
    r"Instead of relying on usernames, passwords, browser-stored credentials, or VPN-only access, "
    r"DNA-Nexus Server uses \*\*QR-based login approval through DNA-Messenger\*\*\. "
    r"The phone acts as the trust anchor, and the server does not trust the browser alone\.\n",
    re.M,
)
s = old_intro.sub(
"""DNA-Nexus Server keeps authorization anchored to a DNA fingerprint while supporting multiple browser login methods. QR-based DNA Connect login remains the flagship identity-first flow, classic password login can be used when configured, and OPAQUE provides a zero-knowledge password login path where the plaintext password is processed locally in the browser and is not sent to the server.

The browser is treated as an interface into the fingerprint-backed session model, not as the identity itself.
""",
    s,
    count=1,
)

# Ecosystem bullets: normalize arrows/question marks if they exist.
s = s.replace(
"- **DNA-Messenger** ? identity, secure messaging, and QR-based authentication",
"- **DNA-Messenger** - identity, secure messaging, and QR-based authentication",
)
s = s.replace(
"- **PQ-SSH** ? identity-based SSH access",
"- **PQ-SSH** - identity-based SSH access",
)
s = s.replace(
"- **DNA-Nexus Server** ? identity-secured storage, sharing, apps, and collaboration",
"- **DNA-Nexus Server** - identity-secured storage, sharing, apps, and collaboration",
)

s = s.replace(
"**The phone proves identity, not the browser.**",
"**Identity is the anchor. The login method is only the entry path.**",
)

replace_section("Key Design Goals", """# Key Design Goals

DNA-Nexus Server is designed around a few strong principles:

- one internal identity model based on DNA fingerprints
- multiple login methods converging to the same fingerprint-backed session
- QR / DNA Connect login for device-mediated identity approval
- OPAQUE login for zero-knowledge password authentication
- optional classic password login when explicitly configured
- no silent fallback from one login method to another
- no unnecessary heavyweight services
- no forced cloud dependency
- no complex enterprise stack for simple personal storage
- identity belongs to the user, not the server
- access decisions are tied to DNA fingerprints
- authentication and recovery flows fail closed when incomplete or misconfigured

The goal is to keep the system small, transparent, understandable, and secure by design.
""")

replace_section("Authentication Model", """# Authentication Model

DNA-Nexus Server keeps one internal identity and authorization model.

The internal identity anchor is the DNA fingerprint. Browser login methods are entry paths into that same fingerprint-backed session model.

Current browser login modes include:

- QR / DNA Connect login - the browser shows a QR code, a trusted mobile device approves the challenge, and the server mints pqnas_session.
- Classic password login - the browser sends username/email and password to the password endpoint, the server verifies the configured password credential, resolves it to a fingerprint, and mints pqnas_session.
- OPAQUE zero-knowledge password login - the browser performs OPAQUE client cryptographic steps locally, the plaintext password is not sent to the server, the server verifies the OPAQUE transcript, resolves the account to a fingerprint, and mints pqnas_session.

All successful browser login methods converge to:

- fingerprint
- pqnas_session
- the same authorization checks
- the same File Manager, app, workspace, quota, role, and sharing model

This means the rest of the server should not need to care whether the session came from QR login, password login, or OPAQUE login.

Mobile trusted devices use their own bearer-token and refresh-token model. Mobile pairing and app tokens are separate from browser login mode and are not replaced by OPAQUE browser login.

Important authentication rules:

- login attempts must never create users
- disabled or pending users must not be enabled by login or recovery
- OPAQUE mode must not silently fall back to classic password login
- password mode must not accidentally expose QR login if QR login is disabled
- QR mode must not accidentally expose password endpoints as unintended login paths
- the login UI must fail closed when the selected login method is not available

The browser is treated as a user interface. The server still resolves access to the internal fingerprint identity.
""")

replace_section("Non-Goals", """# Non-Goals

DNA-Nexus Server deliberately avoids some traditional NAS directions.

Not primary goals:

- becoming a Kubernetes platform
- becoming a heavy virtualization system
- replacing large enterprise storage clusters
- making traditional password login the only or primary identity model
- depending on a large container stack for normal operation
- hiding the system behind unnecessary complexity

The project may integrate with other tools where useful, but the core server should remain small, direct, and identity-first.
""")

replace_section("Security Philosophy", """# Security Philosophy

DNA-Nexus Server is built around the idea that identity should not be reduced to a browser session alone.

Important principles:

- identity should belong to the user
- access should be tied to cryptographic identity
- browser login methods should resolve to the same internal fingerprint model
- QR login should use a trusted device as the approval anchor
- OPAQUE login should avoid sending plaintext passwords to the server
- classic password login should be explicit, configured, rate-limited, and fail closed
- compromised browser state should not automatically mean full account compromise
- sharing should be explicit and controlled
- external access should be role-based and limited
- admin, recovery, and onboarding flows should avoid partial unsafe state

> Identity is the anchor.
>
> The browser is only the interface.
>
> The server verifies access instead of blindly trusting UI state.
""")

doc_map = """# Documentation Map

The repository documentation is organized by topic under docs/.

Useful entry points:

- docs/README.md - documentation index
- docs/auth/ - login modes, QR authentication, password authentication, mobile authentication, and browser auth notes
- docs/security/ - secure coding baseline, red-team notes, and security guidance
- docs/technical/ - implementation design notes and deep technical documents
- docs/architecture/ - architecture and storage design
- docs/operations/ - operations, troubleshooting, maintenance, and deployment notes
- docs/product/ - product and app-level documentation
- docs/user-guides/ - user-facing guides
- docs/research/ - research notes and exploratory design work

Root-level documentation should stay minimal. New long-form documents should normally go into the most relevant docs/ subdirectory.
"""

insert_before_heading(
    ["Why Not a Traditional NAS?", "Why Not a Traditional NAS", "Current Status", "License"],
    doc_map,
)

s = s.replace(
"> The phone proves identity.\n>\n> DNA-Nexus Server should stay lightweight, transparent, and understandable.",
"> Identity is the anchor.\n>\n> The login method is only the entry path.\n>\n> DNA-Nexus Server should stay lightweight, transparent, and understandable.",
)

p.write_text(s)
print("updated root README.md")
