#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def p(rel: str) -> Path:
    return ROOT / rel

def read(rel: str) -> str:
    path = p(rel)
    if not path.exists():
        die(f"missing file: {rel}")
    return path.read_text(encoding="utf-8")

def write(rel: str, text: str) -> None:
    p(rel).write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}")
    write(rel, text.replace(old, new, 1))

# Header: add login wrappers.
replace_once(
    "server/src/opaque_helper_client.h",
    """    OpaqueHelperClientResult register_start(const std::filesystem::path& setup_path,
                                           const std::string& credential_id,
                                           const std::string& registration_request_b64) const;
    OpaqueHelperClientResult register_finish(const std::string& registration_upload_b64) const;
""",
    """    OpaqueHelperClientResult register_start(const std::filesystem::path& setup_path,
                                           const std::string& credential_id,
                                           const std::string& registration_request_b64) const;
    OpaqueHelperClientResult register_finish(const std::string& registration_upload_b64) const;
    OpaqueHelperClientResult login_start(const std::filesystem::path& setup_path,
                                        const std::string& opaque_password_file_b64,
                                        const std::string& credential_id,
                                        const std::string& credential_request_b64) const;
    OpaqueHelperClientResult login_finish(const std::string& server_login_state_b64,
                                         const std::string& credential_finalization_b64) const;
""",
)

# CPP: allow login commands.
replace_once(
    "server/src/opaque_helper_client.cpp",
    """        if (args[0] == "register-finish") {
            return is_safe_helper_arg(args[1], 262144);
        }
    }

    if (args.size() == 4 && args[0] == "register-start") {
        return is_safe_helper_arg(args[1], 4096) &&
               is_safe_helper_arg(args[2], 512) &&
               is_safe_helper_arg(args[3], 8192);
    }

    return false;
""",
    """        if (args[0] == "register-finish") {
            return is_safe_helper_arg(args[1], 262144);
        }
    }

    if (args.size() == 3 && args[0] == "login-finish") {
        return is_safe_helper_arg(args[1], 16384) &&
               is_safe_helper_arg(args[2], 8192);
    }

    if (args.size() == 4 && args[0] == "register-start") {
        return is_safe_helper_arg(args[1], 4096) &&
               is_safe_helper_arg(args[2], 512) &&
               is_safe_helper_arg(args[3], 8192);
    }

    if (args.size() == 5 && args[0] == "login-start") {
        return is_safe_helper_arg(args[1], 4096) &&
               is_safe_helper_arg(args[2], 262144) &&
               is_safe_helper_arg(args[3], 512) &&
               is_safe_helper_arg(args[4], 8192);
    }

    return false;
""",
)

# CPP: implement login wrapper methods.
replace_once(
    "server/src/opaque_helper_client.cpp",
    """OpaqueHelperClientResult OpaqueHelperClient::register_finish(const std::string& registration_upload_b64) const {
    return run_allowed_command({"register-finish", registration_upload_b64});
}

OpaqueHelperClientResult OpaqueHelperClient::run_allowed_command(const std::vector<std::string>& args) const {
""",
    """OpaqueHelperClientResult OpaqueHelperClient::register_finish(const std::string& registration_upload_b64) const {
    return run_allowed_command({"register-finish", registration_upload_b64});
}

OpaqueHelperClientResult OpaqueHelperClient::login_start(
    const std::filesystem::path& setup_path,
    const std::string& opaque_password_file_b64,
    const std::string& credential_id,
    const std::string& credential_request_b64) const {
    return run_allowed_command({
        "login-start",
        setup_path.string(),
        opaque_password_file_b64,
        credential_id,
        credential_request_b64
    });
}

OpaqueHelperClientResult OpaqueHelperClient::login_finish(
    const std::string& server_login_state_b64,
    const std::string& credential_finalization_b64) const {
    return run_allowed_command({
        "login-finish",
        server_login_state_b64,
        credential_finalization_b64
    });
}

OpaqueHelperClientResult OpaqueHelperClient::run_allowed_command(const std::vector<std::string>& args) const {
""",
)

# Unit test: command allowlist coverage.
replace_once(
    "tests/opaque_helper_client/test_opaque_helper_client.cpp",
    """    const auto malformed_register_finish = client.register_finish("QUJD");
    require_true(!malformed_register_finish.ok,
                 "malformed register-finish payload must fail closed");

    pqnas::OpaqueHelperClient missing(std::filesystem::temp_directory_path() /
""",
    """    const auto malformed_register_finish = client.register_finish("QUJD");
    require_true(!malformed_register_finish.ok,
                 "malformed register-finish payload must fail closed");

    const auto empty_login_start = client.login_start(
        std::filesystem::temp_directory_path() / "opaque_server_setup.bin",
        "",
        "user@example.invalid",
        "QUJD");
    require_true(!empty_login_start.ok, "empty login-start password file must fail closed");
    require_true(empty_login_start.error == "opaque_helper_command_not_allowed",
                 "empty login-start password file should be rejected before exec");

    const auto empty_login_finish = client.login_finish("", "QUJD");
    require_true(!empty_login_finish.ok, "empty login-finish state must fail closed");
    require_true(empty_login_finish.error == "opaque_helper_command_not_allowed",
                 "empty login-finish state should be rejected before exec");

    pqnas::OpaqueHelperClient missing(std::filesystem::temp_directory_path() /
""",
)

replace_once(
    "tests/opaque_helper_client/test_opaque_helper_client.cpp",
    """    std::cout << "ok: OPAQUE helper client scaffold tests passed\\n";
""",
    """    std::cout << "ok: OPAQUE helper client wrapper tests passed\\n";
""",
)

# C++ roundtrip runner: add login modes.
replace_once(
    "tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.cpp",
    """    if (mode == "register-finish") {
        if (argc != 4) {
            fail("usage: <rust-helper> register-finish <registration-upload-b64>");
        }

        const auto result = client.register_finish(argv[3]);
        if (!result.ok) {
            fail("register-finish failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\\"ok\\":true") ||
            !contains(result.output, "\\"opaque_password_file_b64\\"")) {
            fail("register-finish output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    fail("unknown mode: " + mode);
""",
    """    if (mode == "register-finish") {
        if (argc != 4) {
            fail("usage: <rust-helper> register-finish <registration-upload-b64>");
        }

        const auto result = client.register_finish(argv[3]);
        if (!result.ok) {
            fail("register-finish failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\\"ok\\":true") ||
            !contains(result.output, "\\"opaque_password_file_b64\\"")) {
            fail("register-finish output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    if (mode == "login-start") {
        if (argc != 7) {
            fail("usage: <rust-helper> login-start <setup-path> <opaque-password-file-b64> <credential-id> <credential-request-b64>");
        }

        const auto result = client.login_start(argv[3], argv[4], argv[5], argv[6]);
        if (!result.ok) {
            fail("login-start failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\\"ok\\":true") ||
            !contains(result.output, "\\"credential_response_b64\\"") ||
            !contains(result.output, "\\"server_login_state_b64\\"")) {
            fail("login-start output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    if (mode == "login-finish") {
        if (argc != 5) {
            fail("usage: <rust-helper> login-finish <server-login-state-b64> <credential-finalization-b64>");
        }

        const auto result = client.login_finish(argv[3], argv[4]);
        if (!result.ok) {
            fail("login-finish failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\\"ok\\":true") ||
            !contains(result.output, "\\"authenticated\\":true")) {
            fail("login-finish output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    fail("unknown mode: " + mode);
""",
)

# Python orchestrator: extend registration roundtrip into login roundtrip.
replace_once(
    "tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.py",
    """        if not isinstance(password_file_bytes, int) or password_file_bytes <= 0:
            fail(f"register-finish reported invalid byte count: {server_finish_json}")

    print("ok: C++ OpaqueHelperClient to Rust helper registration roundtrip passed")
""",
    """        if not isinstance(password_file_bytes, int) or password_file_bytes <= 0:
            fail(f"register-finish reported invalid byte count: {server_finish_json}")

        client_login_start = cargo_fixture(cargo_manifest, "login-start-fixture")
        client_login_start_json = parse_json(client_login_start.stdout, "client login start fixture")
        client_login_state_b64 = client_login_start_json.get("client_login_state_b64", "")
        credential_request_b64 = client_login_start_json.get("credential_request_b64", "")
        if not client_login_state_b64 or not credential_request_b64:
            fail(f"client login start fixture missing fields: {client_login_start_json}")

        server_login_start = run(
            [
                cpp_runner,
                rust_helper,
                "login-start",
                setup_path,
                password_file_b64,
                "roundtrip@example.invalid",
                credential_request_b64,
            ],
            expect_rc=0,
        )
        server_login_start_json = parse_json(server_login_start.stdout, "C++ wrapper login-start")
        credential_response_b64 = server_login_start_json.get("credential_response_b64", "")
        server_login_state_b64 = server_login_start_json.get("server_login_state_b64", "")
        if not credential_response_b64 or not server_login_state_b64:
            fail(f"login-start missing expected fields: {server_login_start_json}")

        client_login_finish = cargo_fixture(
            cargo_manifest,
            "login-finish-fixture",
            client_login_state_b64,
            credential_response_b64,
        )
        client_login_finish_json = parse_json(client_login_finish.stdout, "client login finish fixture")
        credential_finalization_b64 = client_login_finish_json.get("credential_finalization_b64", "")
        if not credential_finalization_b64:
            fail(f"client login finish fixture missing finalization: {client_login_finish_json}")

        server_login_finish = run(
            [
                cpp_runner,
                rust_helper,
                "login-finish",
                server_login_state_b64,
                credential_finalization_b64,
            ],
            expect_rc=0,
        )
        server_login_finish_json = parse_json(server_login_finish.stdout, "C++ wrapper login-finish")
        if server_login_finish_json.get("authenticated") is not True:
            fail(f"login-finish did not authenticate: {server_login_finish_json}")

    print("ok: C++ OpaqueHelperClient to Rust helper registration+login roundtrip passed")
""",
)

# Docs.
doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## Rust helper login operations\n"
section = """## C++ helper client login wrappers

`OpaqueHelperClient` exposes typed wrappers for the Rust helper login operations:

- `login_start(setup_path, opaque_password_file_b64, credential_id, credential_request_b64)`
- `login_finish(server_login_state_b64, credential_finalization_b64)`

The client wrapper keeps the same fail-closed command allowlist model as the
registration wrappers. Invalid empty, oversized, or control-character arguments
are rejected before `execv()`.

The C++ -> Rust roundtrip test now covers both registration and login at helper
level. Public HTTP OPAQUE login routes are still intentionally disabled until
route integration and session minting are reviewed separately.

"""
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    write(doc, text.replace(anchor, section + anchor, 1))
else:
    print(f"unchanged: {doc}")

print("done")
