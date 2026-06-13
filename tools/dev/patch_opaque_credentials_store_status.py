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
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

# Header fields.
replace_once(
    "server/src/opaque_backend_status.h",
    "#include <filesystem>\n",
    "#include <cstddef>\n#include <filesystem>\n",
)

replace_once(
    "server/src/opaque_backend_status.h",
    """    bool credentials_file_exists = false;
    bool credentials_file_readable = false;

    bool server_setup_file_exists = false;
""",
    """    bool credentials_file_exists = false;
    bool credentials_file_readable = false;
    bool credentials_store_valid = false;
    std::size_t credentials_account_count = 0;

    bool server_setup_file_exists = false;
""",
)

# Backend implementation.
replace_once(
    "server/src/opaque_backend_status.cpp",
    """#include "opaque_backend_status.h"

#include "opaque_helper_client.h"
#include "runtime_paths.h"
""",
    """#include "opaque_backend_status.h"

#include "opaque_credentials.h"
#include "opaque_helper_client.h"
#include "runtime_paths.h"
""",
)

replace_once(
    "server/src/opaque_backend_status.cpp",
    """    st.credentials_file_exists = file_exists_regular(st.credentials_path);
    st.credentials_file_readable = st.credentials_file_exists && file_readable(st.credentials_path);

    st.server_setup_file_exists = file_exists_regular(st.server_setup_path);
""",
    """    st.credentials_file_exists = file_exists_regular(st.credentials_path);
    st.credentials_file_readable = st.credentials_file_exists && file_readable(st.credentials_path);

    if (st.credentials_file_readable) {
        OpaqueCredentials credentials;
        st.credentials_store_valid = credentials.load(st.credentials_path.string());
        if (st.credentials_store_valid) {
            st.credentials_account_count = credentials.size();
        }
    }

    st.server_setup_file_exists = file_exists_regular(st.server_setup_path);
""",
)

replace_once(
    "server/src/opaque_backend_status.cpp",
    """    if (!st.credentials_file_exists) {
        add_missing(st.missing_or_not_ready, "opaque_credentials_missing");
    } else if (!st.credentials_file_readable) {
        add_missing(st.missing_or_not_ready, "opaque_credentials_not_readable");
    }

    if (!st.server_setup_file_exists) {
""",
    """    if (!st.credentials_file_exists) {
        add_missing(st.missing_or_not_ready, "opaque_credentials_missing");
    } else if (!st.credentials_file_readable) {
        add_missing(st.missing_or_not_ready, "opaque_credentials_not_readable");
    } else if (!st.credentials_store_valid) {
        add_missing(st.missing_or_not_ready, "opaque_credentials_invalid");
    }

    if (!st.server_setup_file_exists) {
""",
)

replace_once(
    "server/src/opaque_backend_status.cpp",
    """        << "\\"credentials_file_exists\\":" << json_bool(status.credentials_file_exists) << ','
        << "\\"credentials_file_readable\\":" << json_bool(status.credentials_file_readable) << ','
        << "\\"server_setup_file_exists\\":" << json_bool(status.server_setup_file_exists) << ','
""",
    """        << "\\"credentials_file_exists\\":" << json_bool(status.credentials_file_exists) << ','
        << "\\"credentials_file_readable\\":" << json_bool(status.credentials_file_readable) << ','
        << "\\"credentials_store_valid\\":" << json_bool(status.credentials_store_valid) << ','
        << "\\"credentials_account_count\\":" << status.credentials_account_count << ','
        << "\\"server_setup_file_exists\\":" << json_bool(status.server_setup_file_exists) << ','
""",
)

# CMake: backend-status test now depends on opaque_credentials.cpp + nlohmann include path.
replace_once(
    "CMakeLists.txt",
    """add_executable(test_opaque_backend_status
        tests/opaque_backend_status/test_opaque_backend_status.cpp
        server/src/opaque_backend_status.cpp
        server/src/opaque_helper_client.cpp
        server/src/runtime_paths.cpp
)
""",
    """add_executable(test_opaque_backend_status
        tests/opaque_backend_status/test_opaque_backend_status.cpp
        server/src/opaque_backend_status.cpp
        server/src/opaque_helper_client.cpp
        server/src/opaque_credentials.cpp
        server/src/runtime_paths.cpp
)
""",
)

replace_once(
    "CMakeLists.txt",
    """target_include_directories(test_opaque_backend_status PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)
""",
    """target_include_directories(test_opaque_backend_status PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
        ${CMAKE_SOURCE_DIR}/server/third_party
)
""",
)

# Tests.
rel = "tests/opaque_backend_status/test_opaque_backend_status.cpp"
text = read(rel)

old = '''    require_true(present.credentials_file_exists, "credentials file should exist");
    require_true(present.credentials_file_readable, "credentials file should be readable");
    require_true(present.server_setup_file_exists, "server setup file should exist");
'''
new = '''    require_true(present.credentials_file_exists, "credentials file should exist");
    require_true(present.credentials_file_readable, "credentials file should be readable");
    require_true(present.credentials_store_valid, "credentials store should parse as valid");
    require_true(present.credentials_account_count == 0, "empty credentials store should report zero accounts");
    require_true(present.server_setup_file_exists, "server setup file should exist");
'''
if new not in text:
    if old not in text:
        die("present credentials assertion anchor not found")
    text = text.replace(old, new, 1)

old = '''    require_true(contains_text(present_diag, "\\"helper_version_ok\\":true"),
                 "internal diagnostic should report helper version success");
'''
new = '''    require_true(contains_text(present_diag, "\\"credentials_store_valid\\":true"),
                 "internal diagnostic should report credentials store parse success");
    require_true(contains_text(present_diag, "\\"credentials_account_count\\":0"),
                 "internal diagnostic should report empty credentials account count");
    require_true(contains_text(present_diag, "\\"helper_version_ok\\":true"),
                 "internal diagnostic should report helper version success");
'''
if new not in text:
    if old not in text:
        die("present diagnostic credentials anchor not found")
    text = text.replace(old, new, 1)

marker = '''    write_file(helper,
               "#!/bin/sh\\n"
               "if [ \\"$1\\" = \\"--version\\" ]; then\\n"
'''
invalid_block = '''    write_file(credentials,
               "{\\n"
               "  \\"version\\": 1,\\n"
               "  \\"accounts\\": [\\n"
               "    {\\n"
               "      \\"login\\": \\"user@example.com\\",\\n"
               "      \\"fingerprint\\": \\"abcdef123456\\",\\n"
               "      \\"opaque_password_file_b64\\": \\"abc\\",\\n"
               "      \\"opaque_suite\\": \\"suite\\",\\n"
               "      \\"password_hash\\": \\"$argon2id$must-not-exist-here\\"\\n"
               "    }\\n"
               "  ]\\n"
               "}\\n");
    auto invalid_credentials = pqnas::check_opaque_backend_status();
    require_true(!invalid_credentials.credentials_store_valid,
                 "credentials store with password_hash fallback must fail closed");
    require_true(contains_reason(invalid_credentials.missing_or_not_ready, "opaque_credentials_invalid"),
                 "invalid credentials reason should be reported internally");

    write_file(credentials, "{ \\"version\\": 1, \\"accounts\\": [] }\\n");

'''
if "credentials store with password_hash fallback must fail closed" not in text:
    pos = text.find(marker)
    if pos == -1:
        die("could not find second-helper marker for invalid credentials block")
    text = text[:pos] + invalid_block + text[pos:]

write(rel, text)
print(f"patched: {rel}")

print("done")
