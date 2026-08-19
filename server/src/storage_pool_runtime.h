#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace pqnas {

struct StoragePoolRuntimeEntry {
    std::string pool_id;
    std::filesystem::path mount;
};

// Reset Node-local runtime pool state.
//
// Security: the default data root is injected by trusted startup wiring.
// Invalid paths fail without replacing the current runtime state.
bool storage_pool_runtime_reset(
    const std::filesystem::path& default_data_root
);

// Register one named local pool mount.
//
// "default" is reserved as a compatibility alias for the implicit default
// pool and may never be registered as a named pool.
bool storage_pool_runtime_set_named_mount(
    const std::string& pool_id,
    const std::filesystem::path& mount
);

// Resolve a named pool to its local mount.
// The implicit/default pool is intentionally not returned by this function.
bool storage_pool_runtime_get_named_mount(
    const std::string& pool_id,
    std::filesystem::path* mount
);

// Strict authorization-safe data-root resolution.
//
// Empty pool id is the persisted representation of the default pool.
// "default" is accepted only as the existing runtime compatibility alias.
//
// Security: an unknown or invalid named pool fails closed. It is never
// substituted with the default data root.
bool resolve_storage_pool_data_root_strict(
    const std::string& pool_id,
    std::filesystem::path* data_root
);

// Snapshot named pools only. The implicit default pool is excluded.
std::vector<StoragePoolRuntimeEntry>
storage_pool_runtime_snapshot();

} // namespace pqnas
