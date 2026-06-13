#!/usr/bin/env python3
import json
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

base = os.environ.get("PQNAS_TEST_BASE_URL") or "https://pqnas-dev.pqnas-test.uk"
cookie_path = Path(os.environ.get("PQNAS_TEST_COOKIE_JAR") or "/tmp/pqnas.cookies")

def skip(message):
    print("SKIP: " + message)
    raise SystemExit(0)

if not cookie_path.exists():
    skip(f"cookie jar missing: {cookie_path}")

def fail(message):
    raise SystemExit("FAIL: " + message)

def require(cond, msg):
    if not cond:
        fail(msg)

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
        fail("curl failed: " + proc.stderr.strip())

    status = 0
    for line in raw_headers.splitlines():
        if line.startswith("HTTP/"):
            parts = line.split()
            if len(parts) >= 2 and parts[1].isdigit():
                status = int(parts[1])

    try:
        parsed = json.loads(proc.stdout)
    except Exception:
        fail("response was not JSON: " + proc.stdout[:500])

    return status, raw_headers, parsed

def no_session_cookie(raw_headers):
    return "pqnas_session=" not in raw_headers

def cargo_fixture(cargo_manifest, *args):
    proc = subprocess.run(
        [
            "cargo",
            "run",
            "--quiet",
            "--manifest-path",
            str(cargo_manifest),
            "--bin",
            "opaque_client_fixture",
            "--",
            *args,
        ],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    if proc.returncode != 0:
        fail(
            "opaque_client_fixture failed\n"
            + "stdout:\n" + proc.stdout
            + "\nstderr:\n" + proc.stderr
        )

    try:
        parsed = json.loads(proc.stdout)
    except Exception:
        fail("opaque_client_fixture stdout was not JSON: " + proc.stdout[:500])

    require(parsed.get("ok") is True, f"opaque_client_fixture returned failure: {parsed}")
    return parsed

def main():
    if len(sys.argv) != 2:
        fail("usage: test_opaque_admin_registration_positive_runtime.py <cargo-manifest>")

    cargo_manifest = Path(sys.argv[1])
    if not cargo_manifest.exists():
        fail(f"missing Cargo manifest: {cargo_manifest}")

    status_code, headers, before = request_json(
        "GET",
        "/api/admin/auth/opaque/status",
    )

    require(status_code == 200, f"admin OPAQUE status must return 200, got {status_code}: {before}")
    require(before.get("ok") is True, "admin OPAQUE status ok must be true")
    require(before.get("ready_for_login") is False, "ready_for_login must start false")
    require(no_session_cookie(headers), "status must not set session cookie")

    before_count = before.get("credentials_account_count")
    require(isinstance(before_count, int), "credentials_account_count must be int")

    not_ready = before.get("missing_or_not_ready") or []
    allowed_not_ready = {"opaque_real_login_not_implemented"}
    blocking_not_ready = [x for x in not_ready if x not in allowed_not_ready]
    require(
        not blocking_not_ready,
        f"OPAQUE backend is not ready for positive registration test: {blocking_not_ready}; full={not_ready}",
    )

    env_login = os.environ.get("PQNAS_OPAQUE_TEST_LOGIN", "").strip()
    env_fingerprint = os.environ.get("PQNAS_OPAQUE_TEST_FINGERPRINT", "").strip()

    created_user = False
    if env_login and env_fingerprint:
        login = env_login
        fingerprint = env_fingerprint
    else:
        unique = f"{int(time.time())}-{os.getpid()}"
        login = f"opaque-runtime-{unique}@example.invalid"
        password = "OpaqueRuntimeTestPassword-2026!"

        status_code, headers, user_body = request_json(
            "POST",
            "/api/admin/users/password-create",
            {
                "login": login,
                "password": password,
                "name": "OPAQUE runtime test user",
                "role": "user",
                "status": "disabled",
                "quota_bytes": 0,
            },
        )

        if status_code == 404 and user_body.get("error") == "password_auth_disabled":
            skip(
                "password-create is disabled in this login mode; "
                "set PQNAS_OPAQUE_TEST_LOGIN and PQNAS_OPAQUE_TEST_FINGERPRINT "
                "to use an existing disabled test user"
            )

        require(status_code == 200, f"test user creation must return 200, got {status_code}: {user_body}")
        require(user_body.get("ok") is True, f"test user creation failed: {user_body}")
        require(user_body.get("login") == login, f"created login mismatch: {user_body}")
        require(user_body.get("status") == "disabled", f"test user should be disabled: {user_body}")
        require(no_session_cookie(headers), "test user creation must not set session cookie")

        fingerprint = user_body.get("fingerprint", "")
        require(isinstance(fingerprint, str) and fingerprint, f"missing fingerprint: {user_body}")
        created_user = True

    client_start = cargo_fixture(cargo_manifest, "registration-start-fixture")
    client_state_b64 = client_start.get("client_state_b64", "")
    registration_request_b64 = client_start.get("registration_request_b64", "")
    require(client_state_b64, f"client start missing client_state_b64: {client_start}")
    require(registration_request_b64, f"client start missing registration_request_b64: {client_start}")

    status_code, headers, start_body = request_json(
        "POST",
        "/api/admin/auth/opaque/registration/start",
        {
            "login": login,
            "fingerprint": fingerprint,
            "registration_request_b64": registration_request_b64,
        },
    )

    require(status_code == 200, f"registration start must return 200, got {status_code}: {start_body}")
    require(start_body.get("ok") is True, f"registration start failed: {start_body}")
    require(start_body.get("login") == login, f"registration start login mismatch: {start_body}")
    require(start_body.get("fingerprint") == fingerprint, f"registration start fingerprint mismatch: {start_body}")
    require(start_body.get("ready_for_login") is False, "registration start must keep ready_for_login false")
    require(no_session_cookie(headers), "registration start must not set session cookie")

    registration_response_b64 = start_body.get("registration_response_b64", "")
    require(registration_response_b64, f"registration start missing response: {start_body}")

    client_finish = cargo_fixture(
        cargo_manifest,
        "registration-finish-fixture",
        client_state_b64,
        registration_response_b64,
    )
    registration_upload_b64 = client_finish.get("registration_upload_b64", "")
    require(registration_upload_b64, f"client finish missing registration_upload_b64: {client_finish}")

    status_code, headers, finish_body = request_json(
        "POST",
        "/api/admin/auth/opaque/registration/finish",
        {
            "login": login,
            "fingerprint": fingerprint,
            "registration_upload_b64": registration_upload_b64,
            "enabled": True,
            "temporary": True,
        },
    )

    require(status_code == 200, f"registration finish must return 200, got {status_code}: {finish_body}")
    require(finish_body.get("ok") is True, f"registration finish failed: {finish_body}")
    require(finish_body.get("login") == login, f"registration finish login mismatch: {finish_body}")
    require(finish_body.get("fingerprint") == fingerprint, f"registration finish fingerprint mismatch: {finish_body}")
    require(finish_body.get("enabled") is True, f"registration finish enabled mismatch: {finish_body}")
    require(finish_body.get("temporary") is True, f"registration finish temporary mismatch: {finish_body}")
    require(finish_body.get("ready_for_login") is False, "registration finish must keep ready_for_login false")
    require(no_session_cookie(headers), "registration finish must not set session cookie")

    status_code, headers, after = request_json(
        "GET",
        "/api/admin/auth/opaque/status",
    )
    require(status_code == 200, f"final admin OPAQUE status must return 200, got {status_code}: {after}")
    require(after.get("ok") is True, "final admin OPAQUE status ok must be true")
    require(after.get("ready_for_login") is False, "ready_for_login changed")

    after_count = after.get("credentials_account_count")
    require(isinstance(after_count, int), "final credentials_account_count must be int")

    if created_user:
        require(
            after_count == before_count + 1,
            f"credential count should increase by 1 for new test user, before={before_count}, after={after_count}",
        )
    else:
        require(
            after_count >= before_count,
            f"credential count unexpectedly decreased, before={before_count}, after={after_count}",
        )

    client_login_start = cargo_fixture(cargo_manifest, "login-start-fixture")
    client_login_state_b64 = client_login_start.get("client_login_state_b64", "")
    credential_request_b64 = client_login_start.get("credential_request_b64", "")
    require(client_login_state_b64, f"client login start missing state: {client_login_start}")
    require(credential_request_b64, f"client login start missing request: {client_login_start}")

    status_code, headers, login_start = request_json(
        "POST",
        "/api/auth/opaque/login/start",
        {
            "login": login,
            "credential_request_b64": credential_request_b64,
        },
    )

    if status_code == 404 and login_start.get("error") == "opaque_auth_disabled":
        require(no_session_cookie(headers), "disabled public OPAQUE login start must not set session cookie")
    else:
        require(status_code == 200, f"public OPAQUE login start must return 200, got {status_code}: {login_start}")
        require(login_start.get("ok") is True, f"public OPAQUE login start failed: {login_start}")
        require(login_start.get("login") == login, f"public OPAQUE login start login mismatch: {login_start}")
        require(login_start.get("ready_for_session") is False, "login start must not be ready for session")
        require(login_start.get("session_minting") is False, "login start must keep session minting disabled")
        require(no_session_cookie(headers), "public OPAQUE login start must not set session cookie")

        opaque_login_id = login_start.get("opaque_login_id", "")
        credential_response_b64 = login_start.get("credential_response_b64", "")
        require(opaque_login_id, f"public OPAQUE login start missing id: {login_start}")
        require(credential_response_b64, f"public OPAQUE login start missing response: {login_start}")

        client_login_finish = cargo_fixture(
            cargo_manifest,
            "login-finish-fixture",
            client_login_state_b64,
            credential_response_b64,
        )
        credential_finalization_b64 = client_login_finish.get("credential_finalization_b64", "")
        require(credential_finalization_b64, f"client login finish missing finalization: {client_login_finish}")

        status_code, headers, login_finish = request_json(
            "POST",
            "/api/auth/opaque/login/finish",
            {
                "opaque_login_id": opaque_login_id,
                "credential_finalization_b64": credential_finalization_b64,
            },
        )

        require(status_code == 200, f"public OPAQUE login finish must return 200, got {status_code}: {login_finish}")
        require(login_finish.get("ok") is True, f"public OPAQUE login finish failed: {login_finish}")
        require(login_finish.get("authenticated") is True, f"public OPAQUE login finish did not authenticate: {login_finish}")
        require(login_finish.get("login") == login, f"public OPAQUE login finish login mismatch: {login_finish}")
        require(login_finish.get("fingerprint") == fingerprint, f"public OPAQUE login finish fingerprint mismatch: {login_finish}")
        require(login_finish.get("ready_for_session") is False, "login finish must not be ready for session")
        require(login_finish.get("session_minting") is False, "login finish must keep session minting disabled")
        require(no_session_cookie(headers), "public OPAQUE login finish must not set session cookie")

        if created_user:
            require(login_finish.get("account_status") == "disabled", f"created test user should remain disabled: {login_finish}")
            require(login_finish.get("login_allowed") is False, f"disabled test user must not be login_allowed: {login_finish}")

    print(
        "PASS: OPAQUE admin positive registration runtime test "
        f"login={login} fingerprint={fingerprint} created_user={created_user}"
    )

if __name__ == "__main__":
    main()
