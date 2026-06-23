#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("docs/security/secure_coding_baseline.md")
if not p.exists():
    print(f"ERROR: missing {p}", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

new_sections = r'''---

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

'''

if "## 32. Shared state files need shared locking" in s:
    print("secure coding baseline already contains OPAQUE red-team sections")
else:
    marker = "\n## Summary\n"
    if marker not in s:
        print("ERROR: Summary marker not found", file=sys.stderr)
        sys.exit(1)
    s = s.replace(marker, "\n" + new_sections + marker, 1)
    print("inserted sections 32-35 before Summary")

summary_old = """- every security fix should be small, testable, and repeatable
- every one-time secret should have the shortest practical lifetime
- every multi-file state change needs rollback, repair, or fail-closed behavior
"""

summary_new = """- every security fix should be small, testable, and repeatable
- every one-time secret should have the shortest practical lifetime
- every one-time token should follow account state changes and replacement flows
- every shared security-sensitive state file needs shared locking
- every multi-step security flow should be owned by the backend when partial
  failure would be unsafe
- every browser crypto feature should fail closed instead of falling back
- every multi-file state change needs rollback, repair, or fail-closed behavior
"""

if summary_old in s and summary_new not in s:
    s = s.replace(summary_old, summary_new, 1)
    print("updated Summary bullets")
elif summary_new in s:
    print("Summary bullets already updated")
else:
    print("WARNING: Summary bullet anchor not found; sections were still inserted")

p.write_text(s)
print("done")
