#include "opaque_backend_status.h"

#include "opaque_helper_client.h"
#include "runtime_paths.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <unistd.h>

namespace pqnas {
namespace {

static bool file_exists_regular(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::is_regular_file(p, ec) && !ec;
}

static bool file_readable(const std::filesystem::path& p) {
    return !p.empty() && ::access(p.string().c_str(), R_OK) == 0;
}

static bool file_executable(const std::filesystem::path& p) {
    return !p.empty() && ::access(p.string().c_str(), X_OK) == 0;
}

static void add_missing(std::vector<std::string>& out, const std::string& item) {
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
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    static constexpr char hex[] = "0123456789abcdef";
                    out << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
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

OpaqueBackendStatus check_opaque_backend_status() {
    OpaqueBackendStatus st;
    st.credentials_path = opaque_credentials_path();
    st.server_setup_path = opaque_server_setup_path();
    st.helper_path = opaque_helper_path();

    st.credentials_file_exists = file_exists_regular(st.credentials_path);
    st.credentials_file_readable = st.credentials_file_exists && file_readable(st.credentials_path);

    st.server_setup_file_exists = file_exists_regular(st.server_setup_path);
    st.server_setup_file_readable = st.server_setup_file_exists && file_readable(st.server_setup_path);

    st.helper_exists = file_exists_regular(st.helper_path);
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

        if (st.server_setup_file_readable) {
            const auto setup_check = client.server_setup_check(st.server_setup_path);
            st.server_setup_valid = setup_check.ok;
            st.server_setup_check_output = setup_check.output;
            if (!setup_check.ok) {
                if (!st.helper_probe_error.empty()) {
                    st.helper_probe_error += ";";
                }
                st.helper_probe_error += setup_check.error;
            }
        }
    }

    if (!st.credentials_file_exists) {
        add_missing(st.missing_or_not_ready, "opaque_credentials_missing");
    } else if (!st.credentials_file_readable) {
        add_missing(st.missing_or_not_ready, "opaque_credentials_not_readable");
    }

    if (!st.server_setup_file_exists) {
        add_missing(st.missing_or_not_ready, "opaque_server_setup_missing");
    } else if (!st.server_setup_file_readable) {
        add_missing(st.missing_or_not_ready, "opaque_server_setup_not_readable");
    } else if (!st.server_setup_valid) {
        add_missing(st.missing_or_not_ready, "opaque_server_setup_invalid");
    }

    if (!st.helper_exists) {
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
    //
    // Even if the files exist, this branch still has only scaffolding:
    // - no production OPAQUE crypto selected/wired
    // - no enrollment flow
    // - no login-start/login-finish integration
    // - no session minting from OPAQUE
    st.ready_for_login = false;
    add_missing(st.missing_or_not_ready, "opaque_real_login_not_implemented");

    return st;
}

std::string opaque_backend_public_error(const OpaqueBackendStatus& status) {
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
        << "\"ready_for_login\":" << json_bool(status.ready_for_login) << ','
        << "\"credentials_path\":" << json_escape(status.credentials_path.string()) << ','
        << "\"server_setup_path\":" << json_escape(status.server_setup_path.string()) << ','
        << "\"helper_path\":" << json_escape(status.helper_path.string()) << ','
        << "\"credentials_file_exists\":" << json_bool(status.credentials_file_exists) << ','
        << "\"credentials_file_readable\":" << json_bool(status.credentials_file_readable) << ','
        << "\"server_setup_file_exists\":" << json_bool(status.server_setup_file_exists) << ','
        << "\"server_setup_file_readable\":" << json_bool(status.server_setup_file_readable) << ','
        << "\"server_setup_valid\":" << json_bool(status.server_setup_valid) << ','
        << "\"helper_exists\":" << json_bool(status.helper_exists) << ','
        << "\"helper_executable\":" << json_bool(status.helper_executable) << ','
        << "\"helper_version_ok\":" << json_bool(status.helper_version_ok) << ','
        << "\"helper_self_test_ok\":" << json_bool(status.helper_self_test_ok) << ','
        << "\"helper_probe_error\":" << json_escape(status.helper_probe_error) << ','
        << "\"missing_or_not_ready\":";

    append_json_string_array(out, status.missing_or_not_ready);

    out << '}';

    return out.str();
}

} // namespace pqnas
