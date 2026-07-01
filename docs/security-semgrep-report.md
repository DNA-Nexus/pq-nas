# DNA-Nexus / PQ-NAS Semgrep Security Audit Report

Status: living document  
Started: 2026-07-01  
Scope: Semgrep OSS findings, manual triage, and security hardening fixes.

This document tracks Semgrep findings, manual decisions, and fixes. Keep this
updated whenever a finding is fixed, ignored as known test data, or moved to
the open list.

---

## Current focused Semgrep summary

Latest focused Semgrep state:

| Finding group | Count | Status |
|---|---:|---|
| javascript.audit.detect-replaceall-sanitization.detect-replaceall-sanitization | 61 | Open / pending XSS triage |
| python.lang.security.audit.insecure-file-permissions.insecure-file-permissions | 21 | Open / manual permission triage |
| generic.secrets.security.detected-jwt-token.detected-jwt-token | 0 | Triaged as static auth test vectors |
| python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected | 0 | Fixed |
| python.lang.security.audit.httpsconnection-detected.httpsconnection-detected | 0 | Fixed / documented nosemgrep |
| trailofbits.python.tarfile-extractall-traversal.tarfile-extractall-traversal | 0 | Fixed |

Expected remaining focused findings after current fixes: 82.

---

## Fixed and triaged items

### 1. Update package extraction path traversal

File:

    server/src/updates/pqnas_update_apply.py

Original rule:

    trailofbits.python.tarfile-extractall-traversal.tarfile-extractall-traversal

Status:

    Fixed

Problem:

The update helper used tarfile.extractall(). Even though member names were
already checked, extractall() is a risky pattern in update code because archive
members may attempt path traversal, special file extraction, or writes outside
the intended directory.

Fix:

Replaced extractall() with explicit member-by-member extraction.

Security protection:

- Protects against paths such as ../../etc/pqnas/pqnas.env
- Rejects absolute paths
- Rejects .. path components
- Rejects symlinks, hardlinks, devices, fifos, and other special tar entries
- Verifies every resolved output path stays inside the extraction root

Validation command:

    semgrep scan --config auto server/src/updates/pqnas_update_apply.py

Expected result:

    Findings: 0

---

### 2. smartctl sudo access hardening

Files:

    server/src/storage/pqnas_smartctl_root.sh
    server/src/drive_health.cc
    tools/installer/pqnas_install.py
    tools/release/make_tarball.sh

Original sudoers rule:

    pqnas ALL=(root) NOPASSWD: /usr/sbin/smartctl

Status:

    Fixed

Problem:

The pqnas service user had sudo access to the full /usr/sbin/smartctl command.
The application only needs a small subset of smartctl operations.

Fix:

Added a guarded root wrapper:

    /usr/local/sbin/pqnas-smartctl

Allowed commands:

    pqnas-smartctl --version
    pqnas-smartctl -a -j /dev/<whole-disk>
    pqnas-smartctl -i -j /dev/<whole-disk>
    pqnas-smartctl -t short /dev/<whole-disk>
    pqnas-smartctl -t long /dev/<whole-disk>

Security protection:

- Prevents pqnas_server from executing arbitrary smartctl root commands
- Restricts operations to drive health probes and supported self-tests
- Rejects unsupported arguments such as --scan
- Rejects unsafe device paths and partition nodes
- Moves sudoers permission from the real binary to the guarded wrapper

Runtime validation:

    sudo -u pqnas sudo -n /usr/local/sbin/pqnas-smartctl --version
    sudo -u pqnas sudo -n /usr/local/sbin/pqnas-smartctl --scan
    sudo -u pqnas sudo -n /usr/sbin/smartctl --version
    sudo grep -RIn "smartctl" /etc/sudoers.d /etc/sudoers 2>/dev/null

Expected result:

    /usr/local/sbin/pqnas-smartctl --version  => allowed
    /usr/local/sbin/pqnas-smartctl --scan     => blocked
    /usr/sbin/smartctl --version              => blocked
    sudoers points only to /usr/local/sbin/pqnas-smartctl

---

### 3. Static JWT / auth token test vectors

Files:

    pqnas_qrauth_v4_test_vector_invalid.md
    pqnas_qrauth_v4_test_vectors.md
    pqnas_qrauth_v4_test_vectors_invalid_proof.md
    tests/v4_vectors/vectors.json

Original rule:

    generic.secrets.security.detected-jwt-token.detected-jwt-token

Original count:

    8

Final count:

    0

Status:

    Triaged / ignored as static test fixtures

Reasoning:

These files contain QR-auth and v4-auth test vectors. They are deterministic,
fake, invalid, expired, or fixture tokens used for parser and verifier tests.
They are not live credentials.

Fix:

Added narrow .semgrepignore entries for these specific files.

Security protection:

- Removes known test fixture noise from Semgrep secret scanning
- Keeps real future token leaks visible
- Avoids broad ignores that would hide actual secrets

Important note:

Do not paste real auth tokens or session tokens into logs or chat. Project
specific v4.<payload>.<sig> values may not be caught by generic JWT redaction
regexes.

---

### 4. Telegram notification sender hardening

File:

    tools/runtime/pqnas_notify.py

Original rule:

    python.lang.security.audit.dynamic-urllib-use-detected.dynamic-urllib-use-detected

Follow-up generic rule:

    python.lang.security.audit.httpsconnection-detected.httpsconnection-detected

Status:

    Fixed

Problem:

The Telegram notification sender built a dynamic URL and passed it to urllib.
The intended host was Telegram, but dynamic URL openers can become SSRF or
local-file-read sinks if user-controlled values reach them.

Fix:

Changed Telegram sending to fixed-host HTTPS delivery:

    api.telegram.org

Added validation for:

    telegram_bot_token
    telegram_chat_id

Added explicit TLS context:

    ssl.create_default_context()

A narrow nosemgrep was added only after documenting the fixed host, validated
path components, and explicit TLS context.

Security protection:

- Prevents notification settings from becoming a generic dynamic URL sink
- Keeps the destination host fixed to api.telegram.org
- Validates Telegram token format before constructing the API path
- Validates chat ID / channel username format
- Avoids printing Telegram bot tokens in error messages
- Uses explicit verified TLS context

Validation commands:

    python3 -m py_compile tools/runtime/pqnas_notify.py
    semgrep scan --config auto tools/runtime/pqnas_notify.py

Expected result:

    Findings: 0

---

## Open findings

### A. JavaScript manual HTML escaping

Rule:

    javascript.audit.detect-replaceall-sanitization.detect-replaceall-sanitization

Count:

    61

Status:

    Open

Summary:

Semgrep flags custom replaceAll() HTML escaping helpers.

Security question:

Are escaped values inserted into innerHTML, insertAdjacentHTML, template strings,
or HTML attributes?

Preferred fix direction:

- Use textContent for plain text
- Use DOM element creation instead of HTML string templates where possible
- If HTML is intentionally allowed, use a real sanitizer with a narrow allowlist
- Do not use the same escaping helper for both text-node and attribute contexts

Priority:

    Medium

Notes:

Many findings may be duplicates from the same helper functions. Impact can still
be high if attacker-controlled names, filenames, share names, workspace names, or
audit messages are rendered via innerHTML.

---

### B. Installer file permission warnings

Rule:

    python.lang.security.audit.insecure-file-permissions.insecure-file-permissions

Count:

    21

Status:

    Open / needs manual triage

Summary:

Semgrep flags permissions such as 0755, 0750, and 0700 in the installer.

Initial assessment:

Many are probably acceptable because root-owned executable helpers often need
0755, and private runtime directories often use 0750 or 0700. However,
secret-bearing files and update-related directories should be checked manually.

Security questions:

- Is the path a root-owned executable helper?
- Is the path a non-secret static asset?
- Is the path a secret config file?
- Is the path an update package staging directory?
- Is the path service-writable?
- Could the service user write something that root later executes?

Preferred fix direction:

- Secret files: 0600 root:root where possible
- Runtime config with secrets: avoid world-readable permissions
- Root-owned wrappers: 0755 root:root is usually acceptable
- Service-writable directories: narrow ownership and no world-write
- Update plans/packages: separate attacker-writable staging from root-executed files

Priority:

    Medium

---

## Standard focused Semgrep command

Command:

    semgrep scan \
      --config auto \
      --exclude audit \
      --exclude build \
      --exclude build-release \
      --exclude build-mlkem \
      --exclude cmake-build-debug \
      --exclude cmake-build-release \
      --exclude node_modules \
      --exclude vendor \
      --exclude third_party \
      --exclude server/third_party \
      --exclude tools/opaque_browser_client/target \
      --exclude tests/v5_vectors/v5_vectors.json \
      --exclude apps/bundled/photogallery/src/www/leaflet.js \
      --json \
      --output audit/security/01-sast/semgrep-focused-latest.json \
      .

Grouping command:

    python3 - <<'PY'
    import json
    from pathlib import Path

    path = Path("audit/security/01-sast/semgrep-focused-latest.json")
    data = json.loads(path.read_text(encoding="utf-8"))
    results = data.get("results", [])

    print("total findings:", len(results))
    groups = {}
    for r in results:
        check = r.get("check_id", "")
        groups[check] = groups.get(check, 0) + 1

    for check, count in sorted(groups.items(), key=lambda x: x[1], reverse=True):
        print(f"{count:3d}  {check}")
    PY

---

## Update procedure

Whenever a finding is fixed or triaged:

1. Run focused Semgrep.
2. Update the current summary table.
3. Add a new entry under Fixed and triaged items.
4. Move the item out of Open findings if fully resolved.
5. Include file path, rule id, original risk, fix, security protection, and validation.
6. Commit the report together with the related code/config change.

