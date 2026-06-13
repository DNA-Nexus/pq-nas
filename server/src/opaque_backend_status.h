#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pqnas {

struct OpaqueBackendStatus {
    std::filesystem::path credentials_path;
    std::filesystem::path server_setup_path;
    std::filesystem::path helper_path;

    bool credentials_file_exists = false;
    bool credentials_file_readable = false;

    bool server_setup_file_exists = false;
    bool server_setup_file_readable = false;
    bool server_setup_valid = false;
    std::string server_setup_check_output;

    bool helper_exists = false;
    bool helper_executable = false;
    bool helper_version_ok = false;
    bool helper_self_test_ok = false;
    std::string helper_version_output;
    std::string helper_self_test_output;
    std::string helper_probe_error;

    // This remains false until real OPAQUE crypto, server setup, credential
    // enrollment, and route integration are intentionally implemented.
    bool ready_for_login = false;

    std::vector<std::string> missing_or_not_ready;
};

OpaqueBackendStatus check_opaque_backend_status();

std::string opaque_backend_public_error(const OpaqueBackendStatus& status);

// Internal/admin-only diagnostic JSON. Do not return this from public login
// endpoints because it intentionally includes backend readiness details.
std::string opaque_backend_internal_diagnostic_json(const OpaqueBackendStatus& status);

} // namespace pqnas
