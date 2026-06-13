#!/usr/bin/env python3
import json
import subprocess
import sys
import tempfile
from pathlib import Path

def fail(message):
    raise SystemExit("FAIL: " + message)

def run(args, *, expect_rc=0):
    result = subprocess.run(
        [str(x) for x in args],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    if result.returncode != expect_rc:
        fail(
            "unexpected return code for "
            + repr([str(x) for x in args])
            + f": got {result.returncode}, expected {expect_rc}\n"
            + f"stdout:\n{result.stdout}\n"
            + f"stderr:\n{result.stderr}\n"
        )

    return result

def parse_json(stdout, context):
    try:
        return json.loads(stdout.strip())
    except json.JSONDecodeError as exc:
        fail(f"{context}: stdout was not valid JSON: {exc}\nstdout:\n{stdout}")

def cargo_fixture(manifest, *args):
    return run(
        [
            "cargo",
            "run",
            "--quiet",
            "--manifest-path",
            manifest,
            "--bin",
            "opaque_client_fixture",
            "--",
            *args,
        ],
        expect_rc=0,
    )

def main():
    if len(sys.argv) != 4:
        fail("usage: test_opaque_helper_client_rust_roundtrip.py <cpp-runner> <rust-helper> <cargo-manifest>")

    cpp_runner = Path(sys.argv[1])
    rust_helper = Path(sys.argv[2])
    cargo_manifest = Path(sys.argv[3])

    if not cpp_runner.exists():
        fail(f"missing C++ runner: {cpp_runner}")
    if not rust_helper.exists():
        fail(f"missing Rust helper: {rust_helper}")
    if not cargo_manifest.exists():
        fail(f"missing Cargo manifest: {cargo_manifest}")

    with tempfile.TemporaryDirectory(prefix="pqnas_opaque_cpp_rust_roundtrip.") as tmp:
        setup_path = Path(tmp) / "opaque_server_setup.bin"

        setup = run(
            [rust_helper, "server-setup-create", setup_path],
            expect_rc=0,
        )
        setup_json = parse_json(setup.stdout, "server-setup-create")
        if setup_json.get("ok") is not True:
            fail(f"server setup create did not return ok:true: {setup_json}")

        client_start = cargo_fixture(cargo_manifest, "registration-start-fixture")
        client_start_json = parse_json(client_start.stdout, "client registration start fixture")
        state_b64 = client_start_json.get("client_state_b64", "")
        request_b64 = client_start_json.get("registration_request_b64", "")
        if not state_b64 or not request_b64:
            fail(f"client start fixture missing fields: {client_start_json}")

        server_start = run(
            [
                cpp_runner,
                rust_helper,
                "register-start",
                setup_path,
                "roundtrip@example.invalid",
                request_b64,
            ],
            expect_rc=0,
        )
        server_start_json = parse_json(server_start.stdout, "C++ wrapper register-start")
        response_b64 = server_start_json.get("registration_response_b64", "")
        if not response_b64:
            fail(f"register-start missing registration_response_b64: {server_start_json}")

        client_finish = cargo_fixture(
            cargo_manifest,
            "registration-finish-fixture",
            state_b64,
            response_b64,
        )
        client_finish_json = parse_json(client_finish.stdout, "client registration finish fixture")
        upload_b64 = client_finish_json.get("registration_upload_b64", "")
        if not upload_b64:
            fail(f"client finish fixture missing registration_upload_b64: {client_finish_json}")

        server_finish = run(
            [
                cpp_runner,
                rust_helper,
                "register-finish",
                upload_b64,
            ],
            expect_rc=0,
        )
        server_finish_json = parse_json(server_finish.stdout, "C++ wrapper register-finish")
        password_file_b64 = server_finish_json.get("opaque_password_file_b64", "")
        password_file_bytes = server_finish_json.get("opaque_password_file_bytes", 0)

        if not password_file_b64:
            fail(f"register-finish missing opaque_password_file_b64: {server_finish_json}")
        if not isinstance(password_file_bytes, int) or password_file_bytes <= 0:
            fail(f"register-finish reported invalid byte count: {server_finish_json}")

    print("ok: C++ OpaqueHelperClient to Rust helper registration roundtrip passed")

if __name__ == "__main__":
    main()
