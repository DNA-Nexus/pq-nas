#include "opaque_backend_status.h"

#include "opaque_helper_client.h"
#include "runtime_paths.h"

#include <filesystem>
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

} // namespace pqnas
