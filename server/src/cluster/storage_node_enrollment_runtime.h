#pragma once

#include "cluster/server_role.h"
#include "cluster/storage_node_enrollment_client.h"

#include <cstdint>
#include <functional>
#include <string>

namespace pqnas::cluster {

struct StorageNodeEnrollmentRuntimeConfig {
    bool enabled = false;

    std::string enrollment_id;
    std::uint64_t expected_generation = 0;
    std::string api_base_url;

    // Public TLS identity of this Storage Node.
    // Required whenever enrollment is enabled.
    std::string tls_spki_sha256_pin;

    // Optional during recovery after a credential has already been persisted.
    // Fresh enrollment still requires a valid token file.
    std::string bootstrap_token_file;
};

enum class StorageNodeEnrollmentRuntimeConfigResult {
    Ok,
    InvalidInput
};

const char*
storage_node_enrollment_runtime_config_result_code(
    StorageNodeEnrollmentRuntimeConfigResult result
) noexcept;

// Parses explicit values separately from getenv() so validation is testable.
// All-empty values disable provisioning and preserve legacy node startup.
StorageNodeEnrollmentRuntimeConfigResult
parse_storage_node_enrollment_runtime_config(
    const std::string& raw_enrollment_id,
    const std::string& raw_generation,
    const std::string& raw_api_base_url,
    const std::string& raw_tls_spki_sha256_pin,
    const std::string& raw_bootstrap_token_file,
    StorageNodeEnrollmentRuntimeConfig* output
);

StorageNodeEnrollmentRuntimeConfigResult
load_storage_node_enrollment_runtime_config_from_env(
    StorageNodeEnrollmentRuntimeConfig* output
);

enum class StorageNodeEnrollmentRuntimeCode {
    Disabled,
    Ok,
    InvalidRole,
    InvalidConfig,
    StateUnavailable,
    BootstrapTokenUnavailable,
    AuthenticationFailed,
    DirectorConflict,
    RateLimited,
    DirectorUnavailable,
    ProtocolError,
    PersistFailed
};

const char* storage_node_enrollment_runtime_code(
    StorageNodeEnrollmentRuntimeCode code
) noexcept;

struct StorageNodeEnrollmentRuntimeResult {
    StorageNodeEnrollmentRuntimeCode code =
        StorageNodeEnrollmentRuntimeCode::
            InvalidConfig;

    bool ok = false;
    bool recovered = false;
    bool already_enrolled = false;

    // Enrollment succeeded, but the one-time token file could not be safely
    // removed. The Director has already invalidated the token, so startup may
    // continue while reporting this stable warning.
    bool token_cleanup_warning = false;

    const char* error_code() const noexcept;
};

struct StorageNodeEnrollmentRuntimeDependencies {
    StorageNodeEnrollmentTransport transport;

    std::function<std::string()>
        now_iso_utc;
};

// Ensures node-local credential provisioning before normal cluster credential
// loading. This function performs no logging and never returns secret values.
StorageNodeEnrollmentRuntimeResult
ensure_storage_node_enrollment(
    const ServerRoleConfig& role_config,
    const StorageNodeEnrollmentRuntimeConfig&
        enrollment_config,
    StorageNodeEnrollmentRuntimeDependencies
        dependencies
);

} // namespace pqnas::cluster
