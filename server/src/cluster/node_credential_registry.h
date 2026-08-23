#pragma once

#include <filesystem>
#include <mutex>
#include <string>

namespace pqnas::cluster {

enum class NodeCredentialRegistryResult {
    Ok,
    InvalidInput,
    NotFound,
    AlreadyExists,
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

    // Compares one presented canonical token with the persisted credential
    // under the registry mutex. The stored token is never returned.
    NodeCredentialRegistryResult matches(
        const std::string& node_id,
        const std::string& plaintext_token,
        bool* matched,
        std::string* error
    ) const;

    // Creates a credential only when the node has no existing entry. The
    // O_CREAT | O_EXCL boundary prevents enrollment races from replacing a
    // credential that another transaction already installed.
    NodeCredentialRegistryResult create_if_absent(
        const std::string& node_id,
        const std::string& plaintext_token,
        std::string* error
    );

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

    // Security: NotFound means credential absence was confirmed against a
    // service-private registry directory and durably synchronized. Callers may
    // therefore use NotFound as an idempotent deletion-retry state.
    NodeCredentialRegistryResult erase(
        const std::string& node_id,
        std::string* error
    );

private:
    mutable std::mutex mutex_;
    std::filesystem::path directory_;
};

} // namespace pqnas::cluster
