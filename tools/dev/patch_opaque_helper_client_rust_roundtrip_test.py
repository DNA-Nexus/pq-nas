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
    path = p(rel)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}")
    write(rel, text.replace(old, new, 1))

write(
    "tools/opaque_helper_rust/src/bin/opaque_client_fixture.rs",
    r'''use std::env;
use std::process;

use base64::{engine::general_purpose::STANDARD as B64, Engine as _};
use opaque_ke::argon2::Argon2;
use opaque_ke::ciphersuite::CipherSuite;
use opaque_ke::{
    ClientRegistration,
    ClientRegistrationFinishParameters,
    RegistrationResponse,
    Ristretto255,
    TripleDh,
};
use rand::rngs::OsRng;
use sha2::Sha512;

const PASSWORD: &[u8] = b"pqnas opaque client fixture password";

struct PqnasOpaqueCipherSuite;

impl CipherSuite for PqnasOpaqueCipherSuite {
    type OprfCs = Ristretto255;
    type KeyExchange = TripleDh<Ristretto255, Sha512>;
    type Ksf = Argon2<'static>;
}

fn json_escape(s: &str) -> String {
    let mut out = String::with_capacity(s.len());

    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => {
                use std::fmt::Write;
                let _ = write!(&mut out, "\\u{:04x}", c as u32);
            }
            c => out.push(c),
        }
    }

    out
}

fn json_error(op: &str, error: &str, message: &str) -> i32 {
    println!(
        "{{\"ok\":false,\"op\":\"{}\",\"error\":\"{}\",\"message\":\"{}\"}}",
        json_escape(op),
        json_escape(error),
        json_escape(message),
    );
    1
}

fn registration_start_fixture() -> i32 {
    let mut rng = OsRng;

    let result =
        match ClientRegistration::<PqnasOpaqueCipherSuite>::start(&mut rng, PASSWORD) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("client registration start failed: {err:?}");
                return json_error("registration-start-fixture", "client_start_failed", &message);
            }
        };

    let state_b64 = B64.encode(result.state.serialize().as_slice());
    let request_b64 = B64.encode(result.message.serialize().as_slice());

    println!(
        "{{\"ok\":true,\"op\":\"registration-start-fixture\",\"client_state_b64\":\"{}\",\"registration_request_b64\":\"{}\"}}",
        json_escape(&state_b64),
        json_escape(&request_b64),
    );

    0
}

fn registration_finish_fixture(state_b64: &str, response_b64: &str) -> i32 {
    let state_bytes = match B64.decode(state_b64.trim().as_bytes()) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client state base64 decode failed: {err}");
            return json_error("registration-finish-fixture", "client_state_invalid", &message);
        }
    };

    let response_bytes = match B64.decode(response_b64.trim().as_bytes()) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("registration response base64 decode failed: {err}");
            return json_error("registration-finish-fixture", "registration_response_invalid", &message);
        }
    };

    let state =
        match ClientRegistration::<PqnasOpaqueCipherSuite>::deserialize(state_bytes.as_slice()) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("client state deserialize failed: {err:?}");
                return json_error("registration-finish-fixture", "client_state_invalid", &message);
            }
        };

    let response =
        match RegistrationResponse::<PqnasOpaqueCipherSuite>::deserialize(response_bytes.as_slice()) {
            Ok(v) => v,
            Err(err) => {
                let message = format!("registration response deserialize failed: {err:?}");
                return json_error("registration-finish-fixture", "registration_response_invalid", &message);
            }
        };

    let mut rng = OsRng;
    let result = match state.finish(
        &mut rng,
        PASSWORD,
        response,
        ClientRegistrationFinishParameters::default(),
    ) {
        Ok(v) => v,
        Err(err) => {
            let message = format!("client registration finish failed: {err:?}");
            return json_error("registration-finish-fixture", "client_finish_failed", &message);
        }
    };

    let upload_b64 = B64.encode(result.message.serialize().as_slice());

    println!(
        "{{\"ok\":true,\"op\":\"registration-finish-fixture\",\"registration_upload_b64\":\"{}\"}}",
        json_escape(&upload_b64),
    );

    0
}

fn usage() -> i32 {
    eprintln!(
        "Usage:\n  opaque_client_fixture registration-start-fixture\n  opaque_client_fixture registration-finish-fixture <client-state-b64> <registration-response-b64>"
    );
    2
}

fn main() {
    let mut args = env::args();
    let _program = args.next();

    let Some(op) = args.next() else {
        process::exit(usage());
    };

    let rc = match op.as_str() {
        "registration-start-fixture" => {
            if args.next().is_some() {
                usage()
            } else {
                registration_start_fixture()
            }
        }
        "registration-finish-fixture" => {
            let Some(state_b64) = args.next() else {
                process::exit(usage());
            };
            let Some(response_b64) = args.next() else {
                process::exit(usage());
            };

            if args.next().is_some() {
                usage()
            } else {
                registration_finish_fixture(&state_b64, &response_b64)
            }
        }
        _ => usage(),
    };

    process::exit(rc);
}
''',
)

write(
    "tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.cpp",
    r'''#include "opaque_helper_client.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fail("usage: test_opaque_helper_client_rust_roundtrip <rust-helper> <mode> ...");
    }

    const std::filesystem::path helper_path = argv[1];
    const std::string mode = argv[2];

    pqnas::OpaqueHelperClient client(helper_path);

    if (mode == "register-start") {
        if (argc != 6) {
            fail("usage: <rust-helper> register-start <setup-path> <credential-id> <registration-request-b64>");
        }

        const auto result = client.register_start(argv[3], argv[4], argv[5]);
        if (!result.ok) {
            fail("register-start failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\"ok\":true") ||
            !contains(result.output, "\"registration_response_b64\"")) {
            fail("register-start output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    if (mode == "register-finish") {
        if (argc != 4) {
            fail("usage: <rust-helper> register-finish <registration-upload-b64>");
        }

        const auto result = client.register_finish(argv[3]);
        if (!result.ok) {
            fail("register-finish failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\"ok\":true") ||
            !contains(result.output, "\"opaque_password_file_b64\"")) {
            fail("register-finish output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    fail("unknown mode: " + mode);
}
''',
)

write(
    "tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.py",
    r'''#!/usr/bin/env python3
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
''',
)

replace_once(
    "CMakeLists.txt",
    """add_custom_target(run_test_opaque_helper_client
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_opaque_helper_client" "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_opaque_helper_client pqnas_opaque_helper
)


# -----------------------------------------------------------------------------
# Test: test_opaque_backend_status
""",
    """add_custom_target(run_test_opaque_helper_client
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_opaque_helper_client" "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_opaque_helper_client pqnas_opaque_helper
)


# -----------------------------------------------------------------------------
# Integration test: C++ OpaqueHelperClient -> Rust OPAQUE helper registration
# -----------------------------------------------------------------------------
add_executable(test_opaque_helper_client_rust_roundtrip
        tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.cpp
        server/src/opaque_helper_client.cpp
)

target_include_directories(test_opaque_helper_client_rust_roundtrip PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_opaque_helper_client_rust_roundtrip PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if (PQNAS_CARGO)
    add_custom_target(run_test_opaque_helper_client_rust_roundtrip
            COMMAND python3
                "${CMAKE_SOURCE_DIR}/tests/opaque_helper_client_rust_roundtrip/test_opaque_helper_client_rust_roundtrip.py"
                "${CMAKE_BINARY_DIR}/bin/test_opaque_helper_client_rust_roundtrip"
                "${PQNAS_OPAQUE_RUST_BIN}"
                "${PQNAS_OPAQUE_RUST_DIR}/Cargo.toml"
            WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
            DEPENDS test_opaque_helper_client_rust_roundtrip pqnas_opaque_helper_rust
    )
endif()


# -----------------------------------------------------------------------------
# Test: test_opaque_backend_status
""",
)

doc = "docs/technical/opaque_login_design.md"
text = read(doc)
anchor = "## C++ helper client registration wrappers\n"
section = """## C++ to Rust registration roundtrip test

The integration test `run_test_opaque_helper_client_rust_roundtrip`
verifies the full registration message flow across the process boundary:

1. a test-only Rust client fixture creates a client registration request
2. C++ `OpaqueHelperClient::register_start` calls the Rust helper
3. the test-only Rust client fixture finishes the client registration
4. C++ `OpaqueHelperClient::register_finish` calls the Rust helper
5. the helper returns an `opaque_password_file_b64`

The test fixture uses a fixed local test password and is not used by the
production server binary. The production boundary remains unchanged:
`OpaqueHelperClient` does not read users, does not write credentials, and
does not mint sessions.

"""
if section not in text:
    if anchor not in text:
        die("doc anchor not found")
    write(doc, text.replace(anchor, section + anchor, 1))
else:
    print(f"unchanged: {doc}")

print("done")
