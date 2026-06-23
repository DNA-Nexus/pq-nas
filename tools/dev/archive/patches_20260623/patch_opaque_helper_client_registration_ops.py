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

replace_once(
    "server/src/opaque_helper_client.h",
    """    OpaqueHelperClientResult version() const;
    OpaqueHelperClientResult self_test() const;
    OpaqueHelperClientResult server_setup_check(const std::filesystem::path& setup_path) const;
""",
    """    OpaqueHelperClientResult version() const;
    OpaqueHelperClientResult self_test() const;
    OpaqueHelperClientResult server_setup_check(const std::filesystem::path& setup_path) const;
    OpaqueHelperClientResult register_start(const std::filesystem::path& setup_path,
                                           const std::string& credential_id,
                                           const std::string& registration_request_b64) const;
    OpaqueHelperClientResult register_finish(const std::string& registration_upload_b64) const;
""",
)

replace_once(
    "server/src/opaque_helper_client.cpp",
    """bool is_allowed_helper_args(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        return args[0] == "--version" || args[0] == "self-test";
    }

    if (args.size() == 2) {
        return args[0] == "server-setup-check" && !args[1].empty();
    }

    return false;
}
""",
    """bool is_safe_helper_arg(const std::string& s, std::size_t max_len) {
    if (s.empty() || s.size() > max_len) {
        return false;
    }

    for (unsigned char ch : s) {
        if (ch < 0x20 || ch == 0x7f) {
            return false;
        }
    }

    return true;
}

bool is_allowed_helper_args(const std::vector<std::string>& args) {
    if (args.size() == 1) {
        return args[0] == "--version" || args[0] == "self-test";
    }

    if (args.size() == 2) {
        if (args[0] == "server-setup-check") {
            return is_safe_helper_arg(args[1], 4096);
        }

        if (args[0] == "register-finish") {
            return is_safe_helper_arg(args[1], 262144);
        }
    }

    if (args.size() == 4 && args[0] == "register-start") {
        return is_safe_helper_arg(args[1], 4096) &&
               is_safe_helper_arg(args[2], 512) &&
               is_safe_helper_arg(args[3], 8192);
    }

    return false;
}
""",
)

replace_once(
    "server/src/opaque_helper_client.cpp",
    """OpaqueHelperClientResult OpaqueHelperClient::server_setup_check(const std::filesystem::path& setup_path) const {
    return run_allowed_command({"server-setup-check", setup_path.string()});
}

OpaqueHelperClientResult OpaqueHelperClient::run_allowed_command(const std::vector<std::string>& args) const {
""",
    """OpaqueHelperClientResult OpaqueHelperClient::server_setup_check(const std::filesystem::path& setup_path) const {
    return run_allowed_command({"server-setup-check", setup_path.string()});
}

OpaqueHelperClientResult OpaqueHelperClient::register_start(
    const std::filesystem::path& setup_path,
    const std::string& credential_id,
    const std::string& registration_request_b64) const {
    return run_allowed_command({"register-start", setup_path.string(), credential_id, registration_request_b64});
}

OpaqueHelperClientResult OpaqueHelperClient::register_finish(const std::string& registration_upload_b64) const {
    return run_allowed_command({"register-finish", registration_upload_b64});
}

OpaqueHelperClientResult OpaqueHelperClient::run_allowed_command(const std::vector<std::string>& args) const {
""",
)

replace_once(
    "tests/opaque_helper_client/test_opaque_helper_client.cpp",
    """    pqnas::OpaqueHelperClient missing(std::filesystem::temp_directory_path() /
                                      "pqnas_missing_opaque_helper_for_client_test");
""",
    """    const auto empty_register_start =
        client.register_start(std::filesystem::temp_directory_path() / "opaque_server_setup.bin", "", "QUJD");
    require_true(!empty_register_start.ok, "empty register-start credential id must fail closed");
    require_true(empty_register_start.error == "opaque_helper_command_not_allowed",
                 "empty register-start credential id should be rejected before exec");

    const auto empty_register_finish = client.register_finish("");
    require_true(!empty_register_finish.ok, "empty register-finish upload must fail closed");
    require_true(empty_register_finish.error == "opaque_helper_command_not_allowed",
                 "empty register-finish upload should be rejected before exec");

    const auto malformed_register_finish = client.register_finish("QUJD");
    require_true(!malformed_register_finish.ok,
                 "malformed register-finish payload must fail closed");

    pqnas::OpaqueHelperClient missing(std::filesystem::temp_directory_path() /
                                      "pqnas_missing_opaque_helper_for_client_test");
""",
)

doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## Rust helper registration operations\n"
section = """## C++ helper client registration wrappers

`OpaqueHelperClient` exposes safe wrappers for the Rust helper registration
operations:

- `register_start(setup_path, credential_id, registration_request_b64)`
- `register_finish(registration_upload_b64)`

The wrapper keeps an explicit allowlist for helper commands and rejects empty,
oversized, or control-character-containing arguments before `execv`.

This is still not login enablement:

- no public registration endpoint is enabled by this wrapper alone
- no credential store write happens in `OpaqueHelperClient`
- no `pqnas_session` can be minted here
- login helper operations still fail closed

"""
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    write(doc, text.replace(anchor, section + anchor, 1))
else:
    print(f"unchanged: {doc}")

print("done")
