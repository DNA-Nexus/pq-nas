#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {

inline constexpr int
    kStorageUserProjectionSyncStateSchemaVersion = 1;

inline constexpr std::size_t
    kStorageUserProjectionSyncStateMaxEntries = 65536;

inline constexpr std::size_t
    kStorageUserProjectionSyncStateMaxBytes =
        16u * 1024u * 1024u;

// Last Director projection that is known to have been accepted by a Storage
// Node for this user.
//
// Security: this deliberately contains no credentials, profile data, quota,
// filesystem paths or other user authorization material.
struct StorageUserProjectionSyncState {
    std::string fingerprint;
    std::string home_node;
    std::uint64_t revision = 0;
};

enum class StorageUserProjectionSyncStateStoreResult {
    Applied,
    Unchanged,
    Stale,
    Conflict,
    InvalidInput,
    PersistFailed
};

const char*
storage_user_projection_sync_state_store_result_code(
    StorageUserProjectionSyncStateStoreResult result
) noexcept;

class StorageUserProjectionSyncStateStore {
public:
    // Missing file means a valid empty state store.
    //
    // Security: malformed, linked, incorrectly owned or insufficiently
    // private state fails closed without replacing the current snapshot.
    bool load(
        const std::string& path,
        std::string* error
    );

    std::optional<StorageUserProjectionSyncState>
    get(
        const std::string& fingerprint
    ) const;

    std::vector<StorageUserProjectionSyncState>
    snapshot() const;

    // Ensure that the currently visible in-memory snapshot is durably
    // represented on disk.
    //
    // Security: a rename may already have changed the authoritative snapshot
    // when the following parent-directory fsync fails. Reconciliation must
    // heal that uncertainty before performing further authorization writes.
    bool ensure_durable(
        std::string* error
    );

    // Confirm a successfully synchronized Node/revision.
    //
    // Revision contract:
    // - newer revision        -> Applied
    // - same + same Node      -> Unchanged
    // - same + different Node -> Conflict
    // - older revision        -> Stale
    StorageUserProjectionSyncStateStoreResult
    mark_synced(
        const StorageUserProjectionSyncState& state,
        std::string* error
    );

    // Used after a Director user has been successfully revoked from its last
    // known Storage Node during explicit unplacement or deletion.
    //
    // A stale expectation is a successful no-op:
    // return true, expectation_matched=false.
    bool erase_if_current(
        const std::string& fingerprint,
        const std::string& expected_home_node,
        std::uint64_t expected_revision,
        bool* expectation_matched,
        std::string* error
    );

private:
    bool commit_candidate_locked(
        std::unordered_map<
            std::string,
            StorageUserProjectionSyncState
        >&& candidate,
        std::string* error
    );

    mutable std::mutex mutex_;
    std::filesystem::path path_;

    std::unordered_map<
        std::string,
        StorageUserProjectionSyncState
    > states_;

    // rename() may already have made a new snapshot visible when the following
    // directory fsync fails. Exact retries must re-attempt durable persistence.
    bool durability_uncertain_ = false;
};

} // namespace pqnas::cluster
