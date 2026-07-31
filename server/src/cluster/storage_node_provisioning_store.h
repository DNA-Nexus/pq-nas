#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>

namespace pqnas::cluster {

enum class StorageNodeProvisioningStage {
    Prepared,
    CredentialPersisted,
    Enrolled
};

const char* storage_node_provisioning_stage_name(
    StorageNodeProvisioningStage stage
) noexcept;

enum class StorageNodeProvisioningStoreResult {
    Ok,
    InvalidInput,
    NotLoaded,
    StateMismatch,
    AlreadyEnrolled,
    CredentialMissing,
    CredentialInvalid,
    StorageUnsafe,
    IoFailed,
    PersistFailed
};

const char* storage_node_provisioning_store_result_code(
    StorageNodeProvisioningStoreResult result
) noexcept;

struct StorageNodeProvisioningIntent {
    std::string enrollment_id;
    std::uint64_t expected_generation = 0;
    std::string node_id;
    std::string director_url;
    std::string api_base_url;
    int protocol_version = 0;
};

struct StorageNodeProvisioningRecord {
    int version = 1;
    StorageNodeProvisioningStage stage =
        StorageNodeProvisioningStage::Prepared;

    std::string enrollment_id;
    std::uint64_t expected_generation = 0;
    std::string node_id;
    std::string director_url;
    std::string api_base_url;
    int protocol_version = 0;

    std::string prepared_at;
    std::string updated_at;
    std::string enrolled_at;
};

class StorageNodeProvisioningStore {
public:
    // Loads one node-local provisioning state file and its sibling credential
    // file. Missing state and credential files are a valid empty store.
    //
    // Security:
    // - both files must share one absolute, private parent directory
    // - symlinks, non-regular files, hard links and group/other access fail closed
    // - an orphan credential without state fails closed
    bool load(
        const std::string& state_file,
        const std::string& credential_file,
        std::string* error
    );

    // Creates or recovers one exact enrollment transaction. The state is
    // durably committed as Prepared before a credential can be generated.
    // A successful call returns the short-lived plaintext node credential;
    // the caller must clear it with clear_internal_auth_token_text().
    StorageNodeProvisioningStoreResult
    prepare_for_bootstrap(
        const StorageNodeProvisioningIntent& intent,
        const std::string& now_iso,
        std::string* node_credential,
        StorageNodeProvisioningRecord* prepared,
        std::string* error
    );

    // Marks the exact persisted transaction enrolled only after the caller has
    // received a successful Director response. Repeating the same transition is
    // idempotent; a different transaction always fails closed.
    StorageNodeProvisioningStoreResult
    mark_enrolled(
        const StorageNodeProvisioningIntent& intent,
        const std::string& now_iso,
        StorageNodeProvisioningRecord* enrolled,
        std::string* error
    );

    std::optional<
        StorageNodeProvisioningRecord
    > snapshot() const;

private:
    mutable std::mutex mutex_;

    std::filesystem::path directory_;
    std::string state_filename_;
    std::string credential_filename_;

    std::optional<
        StorageNodeProvisioningRecord
    > record_;
};

} // namespace pqnas::cluster
