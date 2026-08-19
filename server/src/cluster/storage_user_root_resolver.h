#pragma once

#include "cluster/storage_user_projection_store.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace pqnas::cluster {

enum class StorageUserRootResolveResult {
    Resolved,
    InvalidInput,
    InvalidConfiguration,
    ProjectionMissing,
    Unauthorized,
    PoolUnavailable,
    InvalidPath,
    InternalError
};

const char* storage_user_root_resolve_result_code(
    StorageUserRootResolveResult result
) noexcept;

// Resolve a logical Storage pool reference to that pool's local data root.
//
// The callback must fail for an unknown named pool. It must never silently
// substitute the default pool for another pool id.
using StoragePoolDataRootResolver =
    std::function<bool(
        const std::string& storage_pool_id,
        std::filesystem::path* data_root
    )>;

struct StorageUserRootResolverConfig {
    const StorageUserProjectionStore*
        projections = nullptr;

    std::string local_node_id;

    StoragePoolDataRootResolver
        resolve_pool_data_root;
};

struct StorageUserRootResolveOutput {
    std::filesystem::path user_root;

    std::uint64_t projection_revision = 0;
    std::uint64_t quota_bytes = 0;

    std::string storage_pool_id;
    std::string root_rel;
};

class StorageUserRootResolver {
public:
    explicit StorageUserRootResolver(
        StorageUserRootResolverConfig config
    );

    // Resolve only from the fresh Node-local projection.
    //
    // Security:
    // - no UsersRegistry fallback
    // - no default per-fingerprint fallback
    // - no named-pool -> default-pool fallback
    // - disabled, unallocated, moved or missing projections fail closed
    // - the Director-supplied root_rel must remain canonical
    StorageUserRootResolveResult resolve(
        const std::string& fingerprint,
        StorageUserRootResolveOutput* output
    ) const noexcept;

private:
    StorageUserRootResolverConfig config_;
};

} // namespace pqnas::cluster
