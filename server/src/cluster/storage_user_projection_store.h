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
    kStorageUserProjectionStoreSchemaVersion = 1;

inline constexpr std::size_t
    kStorageUserProjectionStoreMaxEntries = 65536;

inline constexpr std::size_t
    kStorageUserProjectionStoreMaxBytes =
        64u * 1024u * 1024u;

inline constexpr std::size_t
    kStorageUserProjectionRootRelMaxBytes = 4096;

struct StorageUserProjection {
    std::string fingerprint;

    bool enabled = false;
    bool storage_allocated = false;

    std::uint64_t quota_bytes = 0;

    std::string root_rel;
    std::string storage_pool_id;
    std::string home_node;

    // Monotonically increasing Director-owned revision.
    //
    // Security: Storage Nodes reject stale or conflicting revisions so delayed
    // control-plane requests cannot overwrite newer authorization metadata.
    std::uint64_t revision = 0;
};

enum class StorageUserProjectionStoreResult {
    Applied,
    Unchanged,
    Stale,
    Conflict,
    InvalidInput,
    PersistFailed
};

const char* storage_user_projection_store_result_code(
    StorageUserProjectionStoreResult result
);

class StorageUserProjectionStore {
public:
    // Missing file means an empty projection store.
    //
    // Security: malformed, linked, incorrectly owned or insufficiently private
    // state fails closed without replacing the current in-memory snapshot.
    bool load(
        const std::string& path,
        std::string* error
    );

    std::optional<StorageUserProjection> get(
        const std::string& fingerprint
    ) const;

    std::vector<StorageUserProjection> snapshot() const;

    // Revision contract:
    // - newer revision        -> Applied
    // - same + identical      -> Unchanged
    // - same + different      -> Conflict
    // - older revision        -> Stale
    StorageUserProjectionStoreResult apply(
        const StorageUserProjection& projection,
        std::string* error
    );

private:
    bool commit_candidate_locked(
        std::unordered_map<
            std::string,
            StorageUserProjection
        >&& candidate,
        std::string* error
    );

    mutable std::mutex mutex_;
    std::filesystem::path path_;

    std::unordered_map<
        std::string,
        StorageUserProjection
    > projections_;

    // rename() may succeed even when the following directory fsync fails.
    // Retrying an identical revision must then re-attempt durable persistence.
    bool durability_uncertain_ = false;
};

} // namespace pqnas::cluster
