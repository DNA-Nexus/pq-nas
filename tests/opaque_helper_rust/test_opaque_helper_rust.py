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

    login = run([str(helper), "login-start"], expect_rc=1)
    login_json = parse_json_stdout(login, "login-start")
    if login_json.get("ok") is not False:
        fail(f"login-start should fail closed: {login_json}")
    if login_json.get("error") != "opaque_backend_not_implemented":
        fail(f"login-start returned wrong error: {login_json}")

    print("ok: Rust OPAQUE helper regression tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
