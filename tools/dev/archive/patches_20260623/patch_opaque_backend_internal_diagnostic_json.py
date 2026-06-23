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

# 1) Header declaration.
replace_once(
    "server/src/opaque_backend_status.h",
    """std::string opaque_backend_public_error(const OpaqueBackendStatus& status);

} // namespace pqnas
""",
    """std::string opaque_backend_public_error(const OpaqueBackendStatus& status);

// Internal/admin-only diagnostic JSON. Do not return this from public login
// endpoints because it intentionally includes backend readiness details.
std::string opaque_backend_internal_diagnostic_json(const OpaqueBackendStatus& status);

} // namespace pqnas
""",
)

# 2) Implementation: small manual JSON serializer, no new dependency.
replace_once(
    "server/src/opaque_backend_status.cpp",
    """#include <filesystem>
#include <string>
#include <unistd.h>
""",
    """#include <filesystem>
#include <sstream>
#include <string>
#include <unistd.h>
""",
)

replace_once(
    "server/src/opaque_backend_status.cpp",
    """static void add_missing(std::vector<std::string>& out, const std::string& item) {
    out.push_back(item);
}

} // namespace
""",
    """static void add_missing(std::vector<std::string>& out, const std::string& item) {
    out.push_back(item);
}

static const char* json_bool(bool v) {
    return v ? "true" : "false";
}

static std::string json_escape(const std::string& s) {
    std::ostringstream out;
    out << '"';

    for (const unsigned char ch : s) {
        switch (ch) {
            case '"': out << "\\\\\\""; break;
            case '\\\\': out << "\\\\\\\\"; break;
            case '\\b': out << "\\\\b"; break;
            case '\\f': out << "\\\\f"; break;
            case '\\n': out << "\\\\n"; break;
            case '\\r': out << "\\\\r"; break;
            case '\\t': out << "\\\\t"; break;
            default:
                if (ch < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out << "\\\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
                } else {
                    out << static_cast<char>(ch);
                }
                break;
        }
    }

    out << '"';
    return out.str();
}

static void append_json_string_array(std::ostringstream& out,
                                     const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out << ',';
        out << json_escape(values[i]);
    }
    out << ']';
}

} // namespace
""",
)

replace_once(
    "server/src/opaque_backend_status.cpp",
    """std::string opaque_backend_public_error(const OpaqueBackendStatus& status) {
    if (status.ready_for_login) {
        return "";
    }

    // Public callers should not learn whether a specific login exists or which
    // exact backend component is missing. Keep detailed reasons internal.
    return "opaque_backend_not_configured";
}

} // namespace pqnas
""",
    """std::string opaque_backend_public_error(const OpaqueBackendStatus& status) {
    if (status.ready_for_login) {
        return "";
    }

    // Public callers should not learn whether a specific login exists or which
    // exact backend component is missing. Keep detailed reasons internal.
    return "opaque_backend_not_configured";
}

std::string opaque_backend_internal_diagnostic_json(const OpaqueBackendStatus& status) {
    std::ostringstream out;

    out << '{'
        << "\\"ready_for_login\\":" << json_bool(status.ready_for_login) << ','
        << "\\"credentials_file_exists\\":" << json_bool(status.credentials_file_exists) << ','
        << "\\"credentials_file_readable\\":" << json_bool(status.credentials_file_readable) << ','
        << "\\"server_setup_file_exists\\":" << json_bool(status.server_setup_file_exists) << ','
        << "\\"server_setup_file_readable\\":" << json_bool(status.server_setup_file_readable) << ','
        << "\\"helper_exists\\":" << json_bool(status.helper_exists) << ','
        << "\\"helper_executable\\":" << json_bool(status.helper_executable) << ','
        << "\\"helper_version_ok\\":" << json_bool(status.helper_version_ok) << ','
        << "\\"helper_self_test_ok\\":" << json_bool(status.helper_self_test_ok) << ','
        << "\\"helper_probe_error\\":" << json_escape(status.helper_probe_error) << ','
        << "\\"missing_or_not_ready\\":";

    append_json_string_array(out, status.missing_or_not_ready);

    out << '}';

    return out.str();
}

} // namespace pqnas
""",
)

# 3) Test helper.
replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """bool contains_reason(const std::vector<std::string>& reasons, const std::string& needle) {
    for (const auto& reason : reasons) {
        if (reason == needle) return true;
    }
    return false;
}
""",
    """bool contains_reason(const std::vector<std::string>& reasons, const std::string& needle) {
    for (const auto& reason : reasons) {
        if (reason == needle) return true;
    }
    return false;
}

bool contains_text(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}
""",
)

replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """    require_true(pqnas::opaque_backend_public_error(missing) == "opaque_backend_not_configured",
                 "public error should be generic");
""",
    """    require_true(pqnas::opaque_backend_public_error(missing) == "opaque_backend_not_configured",
                 "public error should be generic");
    const std::string missing_diag = pqnas::opaque_backend_internal_diagnostic_json(missing);
    require_true(contains_text(missing_diag, "\\"ready_for_login\\":false"),
                 "internal diagnostic should report fail-closed readiness");
    require_true(contains_text(missing_diag, "opaque_credentials_missing"),
                 "internal diagnostic should include missing credential reason");
""",
)

replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """    require_true(pqnas::opaque_backend_public_error(present) == "opaque_backend_not_configured",
                 "public error should remain generic even when files exist");
""",
    """    require_true(pqnas::opaque_backend_public_error(present) == "opaque_backend_not_configured",
                 "public error should remain generic even when files exist");
    const std::string present_diag = pqnas::opaque_backend_internal_diagnostic_json(present);
    require_true(contains_text(present_diag, "\\"helper_version_ok\\":true"),
                 "internal diagnostic should report helper version success");
    require_true(contains_text(present_diag, "\\"helper_self_test_ok\\":true"),
                 "internal diagnostic should report helper self-test success");
    require_true(contains_text(present_diag, "opaque_real_login_not_implemented"),
                 "internal diagnostic should still report real login not implemented");
""",
)

replace_once(
    "tests/opaque_backend_status/test_opaque_backend_status.cpp",
    """    require_true(pqnas::opaque_backend_public_error(broken_helper) == "opaque_backend_not_configured",
                 "public error should remain generic when helper self-test fails");
""",
    """    require_true(pqnas::opaque_backend_public_error(broken_helper) == "opaque_backend_not_configured",
                 "public error should remain generic when helper self-test fails");
    const std::string broken_diag = pqnas::opaque_backend_internal_diagnostic_json(broken_helper);
    require_true(contains_text(broken_diag, "\\"helper_self_test_ok\\":false"),
                 "internal diagnostic should report helper self-test failure");
    require_true(contains_text(broken_diag, "opaque_helper_self_test_failed"),
                 "internal diagnostic should include helper self-test failure reason");
""",
)

# 4) Design doc status update.
replace_once(
    "docs/technical/opaque_login_design.md",
    """- `OpaqueBackendStatus` uses `OpaqueHelperClient` for helper `--version`/`self-test` preflight, but still keeps OPAQUE login fail-closed.
""",
    """- `OpaqueBackendStatus` uses `OpaqueHelperClient` for helper `--version`/`self-test` preflight, but still keeps OPAQUE login fail-closed.
- `OpaqueBackendStatus` has an internal/admin-only diagnostic JSON helper; public OPAQUE login errors remain generic.
""",
)

print("done")
