#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def path(rel: str) -> Path:
    return ROOT / rel

def read(rel: str) -> str:
    p = path(rel)
    if not p.exists():
        die(f"missing file: {rel}")
    return p.read_text(encoding="utf-8")

def write(rel: str, text: str) -> None:
    path(rel).write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

# 1) Extend OpaqueBackendStatus with helper preflight fields.
replace_once(
    "server/src/opaque_backend_status.h",
    """    bool helper_exists = false;
    bool helper_executable = false;

    // This remains false until real OPAQUE crypto, server setup, credential
""",
    """    bool helper_exists = false;
    bool helper_executable = false;
    bool helper_version_ok = false;
    bool helper_self_test_ok = false;
    std::string helper_version_output;
    std::string helper_self_test_output;
    std::string helper_probe_error;

    // This remains false until real OPAQUE crypto, server setup, credential
""",
)

# 2) Wire OpaqueBackendStatus to OpaqueHelperClient, but only for --version/self-test.
replace_once(
    "server/src/opaque_backend_status.cpp",
    """#include "opaque_backend_status.h"

#include "runtime_paths.h"
""",
    """#include "opaque_backend_status.h"

#include "opaque_helper_client.h"
#include "runtime_paths.h"
""",
)

replace_once(
    "server/src/opaque_backend_status.cpp",
    """    st.helper_exists = file_exists_regular(st.helper_path);
    st.helper_executable = st.helper_exists && file_executable(st.helper_path);

    if (!st.credentials_file_exists) {
""",
    """    st.helper_exists = file_exists_regular(st.helper_path);
    st.helper_executable = st.helper_exists && file_executable(st.helper_path);

    if (st.helper_executable) {
        OpaqueHelperClient client(st.helper_path);

        const auto version = client.version();
        st.helper_version_ok = version.ok;
        st.helper_version_output = version.output;
        if (!version.ok) {
            st.helper_probe_error = version.error;
        }

        const auto self_test = client.self_test();
        st.helper_self_test_ok = self_test.ok;
        st.helper_self_test_output = self_test.output;
        if (!self_test.ok) {
            if (!st.helper_probe_error.empty()) {
                st.helper_probe_error += ";";
            }
            st.helper_probe_error += self_test.error;
        }
    }

    if (!st.credentials_file_exists) {
""",
)

replace_once(
    "server/src/opaque_backend_status.cpp",
    """    if (!st.helper_exists) {
        add_missing(st.missing_or_not_ready, "opaque_helper_missing");
    } else if (!st.helper_executable) {
        add_missing(st.missing_or_not_ready, "opaque_helper_not_executable");
    }

    // Deliberately fail closed for now.
""",
    """    if (!st.helper_exists) {
        add_missing(st.missing_or_not_ready, "opaque_helper_missing");
    } else if (!st.helper_executable) {
        add_missing(st.missing_or_not_ready, "opaque_helper_not_executable");
    } else {
        if (!st.helper_version_ok) {
            add_missing(st.missing_or_not_ready, "opaque_helper_version_failed");
        }
        if (!st.helper_self_test_ok) {
            add_missing(st.missing_or_not_ready, "opaque_helper_self_test_failed");
        }
    }

    // Deliberately fail closed for now.
""",
)

# 3) Link OpaqueHelperClient into the backend-status test target.
replace_once(
    "CMakeLists.txt",
    """add_executable(test_opaque_backend_status
        tests/opaque_backend_status/test_opaque_backend_status.cpp
        server/src/opaque_backend_status.cpp
        server/src/runtime_paths.cpp
)
""",
    """add_executable(test_opaque_backend_status
        tests/opaque_backend_status/test_opaque_backend_status.cpp
        server/src/opaque_backend_status.cpp
        server/src/opaque_helper_client.cpp
        server/src/runtime_paths.cpp
)
""",
)

# 4) Strengthen backend-status tests with successful and failing helper preflight cases.
replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """    write_file(helper, "#!/bin/sh\\nexit 0\\n");
    require_true(::chmod(helper.string().c_str(), 0700) == 0,
                 "chmod helper executable should succeed");

    auto present = pqnas::check_opaque_backend_status();
""",
    """    write_file(helper,
               "#!/bin/sh\\n"
               "if [ \\"$1\\" = \\"--version\\" ]; then\\n"
               "  echo \\"pqnas_opaque_helper test-scaffold\\"\\n"
               "  exit 0\\n"
               "fi\\n"
               "if [ \\"$1\\" = \\"self-test\\" ]; then\\n"
               "  echo \\"ok: pqnas_opaque_helper scaffold self-test passed\\"\\n"
               "  exit 0\\n"
               "fi\\n"
               "exit 9\\n");
    require_true(::chmod(helper.string().c_str(), 0700) == 0,
                 "chmod helper executable should succeed");

    auto present = pqnas::check_opaque_backend_status();
""",
)

replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """    require_true(present.helper_exists, "helper should exist");
    require_true(present.helper_executable, "helper should be executable");

    require_true(!present.ready_for_login,
""",
    """    require_true(present.helper_exists, "helper should exist");
    require_true(present.helper_executable, "helper should be executable");
    require_true(present.helper_version_ok, "helper --version preflight should pass");
    require_true(present.helper_self_test_ok, "helper self-test preflight should pass");
    require_true(!contains_reason(present.missing_or_not_ready, "opaque_helper_version_failed"),
                 "successful helper should not report version failure");
    require_true(!contains_reason(present.missing_or_not_ready, "opaque_helper_self_test_failed"),
                 "successful helper should not report self-test failure");

    require_true(!present.ready_for_login,
""",
)

replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """    require_true(pqnas::opaque_backend_public_error(present) == "opaque_backend_not_configured",
                 "public error should remain generic even when files exist");

    fs::remove_all(root, ec);
""",
    """    require_true(pqnas::opaque_backend_public_error(present) == "opaque_backend_not_configured",
                 "public error should remain generic even when files exist");

    write_file(helper,
               "#!/bin/sh\\n"
               "if [ \\"$1\\" = \\"--version\\" ]; then\\n"
               "  echo \\"pqnas_opaque_helper test-scaffold\\"\\n"
               "  exit 0\\n"
               "fi\\n"
               "if [ \\"$1\\" = \\"self-test\\" ]; then\\n"
               "  echo \\"self-test deliberately failed\\"\\n"
               "  exit 42\\n"
               "fi\\n"
               "exit 9\\n");
    require_true(::chmod(helper.string().c_str(), 0700) == 0,
                 "chmod failing helper executable should succeed");

    auto broken_helper = pqnas::check_opaque_backend_status();
    require_true(broken_helper.helper_exists, "failing helper should still exist");
    require_true(broken_helper.helper_executable, "failing helper should still be executable");
    require_true(broken_helper.helper_version_ok, "failing helper version preflight should still pass");
    require_true(!broken_helper.helper_self_test_ok, "failing helper self-test preflight should fail");
    require_true(contains_reason(broken_helper.missing_or_not_ready, "opaque_helper_self_test_failed"),
                 "failing helper self-test reason should be reported internally");
    require_true(!broken_helper.ready_for_login,
                 "backend must remain fail-closed when helper self-test fails");
    require_true(pqnas::opaque_backend_public_error(broken_helper) == "opaque_backend_not_configured",
                 "public error should remain generic when helper self-test fails");

    fs::remove_all(root, ec);
""",
)

# 5) Update design/status doc.
replace_once(
    "docs/technical/opaque_login_design.md",
    """- `OpaqueHelperClient` exists as a C++ helper-client scaffold that can call only `pqnas_opaque_helper --version` and `pqnas_opaque_helper self-test`.
""",
    """- `OpaqueHelperClient` exists as a C++ helper-client scaffold that can call only `pqnas_opaque_helper --version` and `pqnas_opaque_helper self-test`.
- `OpaqueBackendStatus` uses `OpaqueHelperClient` for helper `--version`/`self-test` preflight, but still keeps OPAQUE login fail-closed.
""",
)

print("done")
