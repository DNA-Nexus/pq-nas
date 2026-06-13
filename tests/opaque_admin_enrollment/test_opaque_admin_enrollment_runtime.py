#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path

base = os.environ.get("PQNAS_TEST_BASE_URL") or "https://pqnas-dev.pqnas-test.uk"
cookie_path = Path(os.environ.get("PQNAS_TEST_COOKIE_JAR") or "/tmp/pqnas.cookies")

if not cookie_path.exists():
    print(f"SKIP: cookie jar missing: {cookie_path}")
    sys.exit(0)

def require(cond, msg):
    if not cond:
        raise SystemExit("FAIL: " + msg)

def request_json(method, path, body=None):
    with tempfile.NamedTemporaryFile() as headers_file:
        cmd = [
            "curl",
            "-k",
            "-sS",
            "-X", method,
            "-b", str(cookie_path),
            "-D", headers_file.name,
            "-H", "Accept: application/json",
        ]

        if body is not None:
            cmd += [
                "-H", "Content-Type: application/json",
                "-d", json.dumps(body),
            ]

        cmd.append(base.rstrip("/") + path)

        proc = subprocess.run(
            cmd,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )

        raw_headers = Path(headers_file.name).read_text(
            encoding="utf-8",
            errors="replace",
        )

    if proc.returncode != 0:
        raise SystemExit("FAIL: curl failed: " + proc.stderr.strip())

    status = 0
    for line in raw_headers.splitlines():
        if line.startswith("HTTP/"):
            parts = line.split()
            if len(parts) >= 2 and parts[1].isdigit():
                status = int(parts[1])

    try:
        parsed = json.loads(proc.stdout)
    except Exception:
        raise SystemExit("FAIL: response was not JSON: " + proc.stdout[:500])

    return status, raw_headers, parsed

def no_session_cookie(raw_headers):
    return "pqnas_session=" not in raw_headers

status_code, headers, before = request_json(
    "GET",
    "/api/admin/auth/opaque/status",
)

require(
    status_code == 200,
    f"admin OPAQUE status must return 200, got {status_code}: {before}",
)
require(before.get("ok") is True, "admin OPAQUE status ok must be true")
require(before.get("ready_for_login") is False, "ready_for_login must stay false")

before_count = before.get("credentials_account_count")
require(isinstance(before_count, int), "credentials_account_count must be int")

status_code, headers, body = request_json(
    "POST",
    "/api/admin/auth/opaque/enrollment/upsert",
    {
        "login": "nobody@example.com",
        "fingerprint": "deadbeef",
        "opaque_password_file_b64": "QUJD",
        "opaque_suite": "test-suite",
    },
)

require(status_code == 404, f"unknown user must return 404, got {status_code}: {body}")
require(body.get("ok") is False, "unknown user ok must be false")
require(body.get("error") == "user_not_found", "unknown user error mismatch")
require(no_session_cookie(headers), "unknown user path must not set session cookie")

status_code, headers, body = request_json(
    "POST",
    "/api/admin/auth/opaque/enrollment/upsert",
    {
        "login": "nobody@example.com",
        "fingerprint": "deadbeef",
        "opaque_password_file_b64": "QUJD",
        "password_hash": "must-not-be-accepted",
    },
)

require(status_code == 400, f"password_hash field must return 400, got {status_code}: {body}")
require(
    body.get("error") == "forbidden_password_fallback_field",
    "password fallback error mismatch",
)
require(no_session_cookie(headers), "forbidden fallback path must not set session cookie")

status_code, headers, after = request_json(
    "GET",
    "/api/admin/auth/opaque/status",
)

require(status_code == 200, f"final admin OPAQUE status must return 200, got {status_code}: {after}")
require(after.get("ready_for_login") is False, "ready_for_login changed")
require(
    after.get("credentials_account_count") == before_count,
    "credential account count changed unexpectedly",
)

print("PASS: OPAQUE admin enrollment runtime regression")
