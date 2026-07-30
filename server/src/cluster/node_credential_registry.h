#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace pqnas::cluster {

enum class NodeCredentialRegistryResult {
    Ok,
    InvalidInput,
    NotFound,
    CredentialInvalid,
    StorageUnsafe,
    IoFailed,
    PersistFailed
};

const char* node_credential_registry_result_code(
    NodeCredentialRegistryResult result
) noexcept;

class NodeCredentialRegistry {
public:
    // A missing directory is a valid empty registry. Existing directories and
    // every credential entry are validated before replacing the active path.
    bool load(
        const std::string& directory,
        std::string* error
    );

    NodeCredentialRegistryResult contains(
        const std::string& node_id,
        bool* present,
        std::string* error
    ) const;

    // Writes one canonical plaintext token to a service-private per-node file.
    // The token is never retained in a copyable registry record or snapshot.
    NodeCredentialRegistryResult upsert(
        const std::string& node_id,
        const std::string& plaintext_token,
        std::string* error
    );

    // Exposes the credential only as the short-lived Authorization value needed
    // for one Director-to-node request. Callers must securely clear the output.
    NodeCredentialRegistryResult make_bearer_authorization(
        const std::string& node_id,
        std::string* authorization,
        std::string* error
    ) const;

    NodeCredentialRegistryResult erase(
        const std::string& node_id,
        std::string* error
    );

private:
    mutable std::mutex mutex_;
    std::filesystem::path directory_;
};

} // namespace pqnas::cluster
