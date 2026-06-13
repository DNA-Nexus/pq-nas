#!/usr/bin/env python3
import json
import os
import stat
import subprocess
import sys
import tempfile
from pathlib import Path


def fail(message: str) -> None:
    print(f"FAIL: {message}", file=sys.stderr)
    sys.exit(1)


def run(args, *, expect_rc=None):
    result = subprocess.run(
        args,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    if expect_rc is not None and result.returncode != expect_rc:
        fail(
            "unexpected return code for "
            + repr(args)
            + f": got {result.returncode}, expected {expect_rc}\n"
            + f"stdout:\n{result.stdout}\n"
            + f"stderr:\n{result.stderr}\n"
        )

    return result


def parse_json_stdout(result, context: str):
    try:
        return json.loads(result.stdout.strip())
    except json.JSONDecodeError as exc:
        fail(f"{context}: stdout was not valid JSON: {exc}\nstdout:\n{result.stdout}")


def cargo_fixture(*fixture_args):
    repo_root = Path(__file__).resolve().parents[2]
    manifest = repo_root / "tools" / "opaque_helper_rust" / "Cargo.toml"

    result = run(
        [
            "cargo",
            "run",
            "--quiet",
            "--manifest-path",
            str(manifest),
            "--bin",
            "opaque_client_fixture",
            "--",
            *fixture_args,
        ],
        expect_rc=0,
    )
    return parse_json_stdout(result, "opaque_client_fixture " + " ".join(fixture_args[:1]))


def main() -> int:
    if len(sys.argv) != 2:
        fail("usage: test_opaque_helper_rust.py <pqnas_opaque_helper_rust_binary>")

    helper = Path(sys.argv[1])
    if not helper.exists():
        fail(f"helper binary does not exist: {helper}")
    if not os.access(helper, os.X_OK):
        fail(f"helper binary is not executable: {helper}")

    version = run([str(helper), "--version"], expect_rc=0)
    if "pqnas_opaque_helper" not in version.stdout:
        fail(f"version output did not include program name: {version.stdout!r}")

    self_test = run([str(helper), "self-test"], expect_rc=0)
    if "rust scaffold self-test passed" not in self_test.stdout:
        fail(f"unexpected self-test output: {self_test.stdout!r}")

    with tempfile.TemporaryDirectory(prefix="pqnas_opaque_helper_rust_test.") as tmp:
        setup_path = Path(tmp) / "opaque_server_setup.bin"

        create = run([str(helper), "server-setup-create", str(setup_path)], expect_rc=0)
        create_json = parse_json_stdout(create, "server-setup-create")
        if create_json.get("ok") is not True:
            fail(f"server-setup-create did not return ok:true: {create_json}")
        if create_json.get("op") != "server-setup-create":
            fail(f"server-setup-create returned wrong op: {create_json}")
        if create_json.get("path") != str(setup_path):
            fail(f"server-setup-create returned wrong path: {create_json}")

        if not setup_path.exists():
            fail(f"server setup file was not created: {setup_path}")

        actual_size = setup_path.stat().st_size
        if actual_size <= 0:
            fail(f"server setup file is empty: {setup_path}")

        reported_size = create_json.get("bytes_written")
        if reported_size != actual_size:
            fail(f"bytes_written mismatch: json={reported_size}, stat={actual_size}")

        mode = stat.S_IMODE(setup_path.stat().st_mode)
        if mode != 0o600:
            fail(f"server setup file mode was {oct(mode)}, expected 0o600")

        check = run([str(helper), "server-setup-check", str(setup_path)], expect_rc=0)
        check_json = parse_json_stdout(check, "server-setup-check")
        if check_json.get("ok") is not True:
            fail(f"server-setup-check did not return ok:true: {check_json}")
        if check_json.get("op") != "server-setup-check":
            fail(f"server-setup-check returned wrong op: {check_json}")
        if check_json.get("path") != str(setup_path):
            fail(f"server-setup-check returned wrong path: {check_json}")
        if check_json.get("bytes_read") != actual_size:
            fail(f"server-setup-check bytes_read mismatch: {check_json}")

        invalid_path = Path(tmp) / "invalid_server_setup.bin"
        invalid_path.write_bytes(b"not a valid opaque server setup")
        invalid = run([str(helper), "server-setup-check", str(invalid_path)], expect_rc=1)
        invalid_json = parse_json_stdout(invalid, "invalid server-setup-check")
        if invalid_json.get("error") != "opaque_server_setup_invalid":
            fail(f"invalid server-setup-check returned wrong error: {invalid_json}")

        missing_path = Path(tmp) / "missing_server_setup.bin"
        missing = run([str(helper), "server-setup-check", str(missing_path)], expect_rc=1)
        missing_json = parse_json_stdout(missing, "missing server-setup-check")
        if missing_json.get("error") != "opaque_server_setup_read_failed":
            fail(f"missing server-setup-check returned wrong error: {missing_json}")

        second = run([str(helper), "server-setup-create", str(setup_path)], expect_rc=1)
        second_json = parse_json_stdout(second, "second server-setup-create")
        if second_json.get("ok") is not False:
            fail(f"second server-setup-create should fail: {second_json}")
        if second_json.get("error") != "opaque_server_setup_create_failed":
            fail(f"second server-setup-create returned wrong error: {second_json}")

        if setup_path.stat().st_size != actual_size:
            fail("server setup file size changed after rejected overwrite attempt")

        bad_register_start = run(
            [
                str(helper),
                "register-start",
                str(setup_path),
                "user@example.invalid",
                "not-base64",
            ],
            expect_rc=1,
        )
        bad_register_start_json = parse_json_stdout(
            bad_register_start,
            "bad register-start",
        )
        if bad_register_start_json.get("error") != "opaque_registration_request_b64_invalid":
            fail(f"bad register-start returned wrong error: {bad_register_start_json}")

        bad_register_finish = run(
            [str(helper), "register-finish", "not-base64"],
            expect_rc=1,
        )
        bad_register_finish_json = parse_json_stdout(
            bad_register_finish,
            "bad register-finish",
        )
        if bad_register_finish_json.get("error") != "opaque_registration_upload_b64_invalid":
            fail(f"bad register-finish returned wrong error: {bad_register_finish_json}")

        client_reg_start = cargo_fixture("registration-start-fixture")
        client_reg_state_b64 = client_reg_start.get("client_state_b64", "")
        registration_request_b64 = client_reg_start.get("registration_request_b64", "")
        if not client_reg_state_b64 or not registration_request_b64:
            fail(f"registration fixture missing fields: {client_reg_start}")

        server_reg_start = run(
            [
                str(helper),
                "register-start",
                str(setup_path),
                "user@example.invalid",
                registration_request_b64,
            ],
            expect_rc=0,
        )
        server_reg_start_json = parse_json_stdout(server_reg_start, "register-start roundtrip")
        registration_response_b64 = server_reg_start_json.get("registration_response_b64", "")
        if not registration_response_b64:
            fail(f"register-start missing registration_response_b64: {server_reg_start_json}")

        client_reg_finish = cargo_fixture(
            "registration-finish-fixture",
            client_reg_state_b64,
            registration_response_b64,
        )
        registration_upload_b64 = client_reg_finish.get("registration_upload_b64", "")
        if not registration_upload_b64:
            fail(f"registration finish fixture missing upload: {client_reg_finish}")

        server_reg_finish = run(
            [str(helper), "register-finish", registration_upload_b64],
            expect_rc=0,
        )
        server_reg_finish_json = parse_json_stdout(server_reg_finish, "register-finish roundtrip")
        opaque_password_file_b64 = server_reg_finish_json.get("opaque_password_file_b64", "")
        if not opaque_password_file_b64:
            fail(f"register-finish missing password file: {server_reg_finish_json}")

        client_login_start = cargo_fixture("login-start-fixture")
        client_login_state_b64 = client_login_start.get("client_login_state_b64", "")
        credential_request_b64 = client_login_start.get("credential_request_b64", "")
        if not client_login_state_b64 or not credential_request_b64:
            fail(f"login start fixture missing fields: {client_login_start}")

        server_login_start = run(
            [
                str(helper),
                "login-start",
                str(setup_path),
                opaque_password_file_b64,
                "user@example.invalid",
                credential_request_b64,
            ],
            expect_rc=0,
        )
        server_login_start_json = parse_json_stdout(server_login_start, "login-start roundtrip")
        credential_response_b64 = server_login_start_json.get("credential_response_b64", "")
        server_login_state_b64 = server_login_start_json.get("server_login_state_b64", "")
        if not credential_response_b64 or not server_login_state_b64:
            fail(f"login-start missing expected fields: {server_login_start_json}")

        client_login_finish = cargo_fixture(
            "login-finish-fixture",
            client_login_state_b64,
            credential_response_b64,
        )
        credential_finalization_b64 = client_login_finish.get("credential_finalization_b64", "")
        if not credential_finalization_b64:
            fail(f"login finish fixture missing finalization: {client_login_finish}")

        server_login_finish = run(
            [
                str(helper),
                "login-finish",
                server_login_state_b64,
                credential_finalization_b64,
            ],
            expect_rc=0,
        )
        server_login_finish_json = parse_json_stdout(server_login_finish, "login-finish roundtrip")
        if server_login_finish_json.get("authenticated") is not True:
            fail(f"login-finish did not authenticate: {server_login_finish_json}")

        bad_login_start = run(
            [
                str(helper),
                "login-start",
                str(setup_path),
                opaque_password_file_b64,
                "user@example.invalid",
                "not-base64",
            ],
            expect_rc=1,
        )
        bad_login_start_json = parse_json_stdout(bad_login_start, "bad login-start")
        if bad_login_start_json.get("error") != "opaque_credential_request_b64_invalid":
            fail(f"bad login-start returned wrong error: {bad_login_start_json}")

        bad_login_finish = run(
            [str(helper), "login-finish", "not-base64", "QUJD"],
            expect_rc=1,
        )
        bad_login_finish_json = parse_json_stdout(bad_login_finish, "bad login-finish")
        if bad_login_finish_json.get("error") != "opaque_server_login_state_b64_invalid":
            fail(f"bad login-finish returned wrong error: {bad_login_finish_json}")

    print("ok: Rust OPAQUE helper regression tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
