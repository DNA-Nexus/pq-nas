#include "storage_pool_runtime.h"

#include "storage_metadata_validation.h"

#include <mutex>
#include <unordered_map>

namespace pqnas {
namespace {

struct StoragePoolRuntimeState {
    std::mutex mutex;
    std::filesystem::path default_data_root;

    std::unordered_map<
        std::string,
        std::filesystem::path
    > named_mounts;
};

StoragePoolRuntimeState& runtime_state() {
    static StoragePoolRuntimeState state;
    return state;
}

bool normalize_absolute_path(
    const std::filesystem::path& input,
    std::filesystem::path* output
) {
    if (!output) {
        return false;
    }

    output->clear();

    if (input.empty() ||
        !input.is_absolute()) {
        return false;
    }

    const std::filesystem::path normalized =
        input.lexically_normal();

    if (normalized.empty() ||
        !normalized.is_absolute()) {
        return false;
    }

    *output = normalized;
    return true;
}

bool valid_named_pool_id(
    const std::string& pool_id
) {
    return
        pool_id != "default" &&
        valid_managed_storage_pool_id(
            pool_id
        );
}

bool path_is_strict_descendant_of(
    const std::filesystem::path& candidate,
    const std::filesystem::path& parent
) {
    const std::filesystem::path normalized_candidate =
        candidate.lexically_normal();

    const std::filesystem::path normalized_parent =
        parent.lexically_normal();

    if (normalized_candidate.empty() ||
        normalized_parent.empty() ||
        !normalized_candidate.is_absolute() ||
        !normalized_parent.is_absolute()) {
        return false;
    }

    auto candidate_it =
        normalized_candidate.begin();

    auto parent_it =
        normalized_parent.begin();

    for (;
         parent_it != normalized_parent.end();
         ++parent_it, ++candidate_it) {
        if (candidate_it ==
                normalized_candidate.end() ||
            *candidate_it != *parent_it) {
            return false;
        }
    }

    // Security: the pool mount must be below the pools directory,
    // not the pools directory itself.
    return
        candidate_it !=
        normalized_candidate.end();
}

} // namespace

bool storage_pool_runtime_reset(
    const std::filesystem::path& default_data_root
) {
    std::filesystem::path normalized;

    if (!normalize_absolute_path(
            default_data_root,
            &normalized)) {
        return false;
    }

    auto& state =
        runtime_state();

    std::lock_guard<std::mutex>
        lock(state.mutex);

    state.default_data_root =
        std::move(normalized);

    state.named_mounts.clear();

    return true;
}

bool storage_pool_runtime_set_named_mount(
    const std::string& pool_id,
    const std::filesystem::path& mount
) {
    if (!valid_named_pool_id(
            pool_id)) {
        return false;
    }

    std::filesystem::path normalized;

    if (!normalize_absolute_path(
            mount,
            &normalized)) {
        return false;
    }

    auto& state =
        runtime_state();

    std::lock_guard<std::mutex>
        lock(state.mutex);

    if (state.default_data_root.empty() ||
        !state.default_data_root.is_absolute()) {
        return false;
    }

    const std::filesystem::path pools_root =
        (
            state.default_data_root
                .parent_path() /
            "pools"
        ).lexically_normal();

    // Security: named pool mounts are authorization-visible filesystem
    // roots. Keep them strictly below the local storage-root pools tree.
    if (!path_is_strict_descendant_of(
            normalized,
            pools_root)) {
        return false;
    }

    state.named_mounts[pool_id] =
        std::move(normalized);

    return true;
}

bool storage_pool_runtime_get_named_mount(
    const std::string& pool_id,
    std::filesystem::path* mount
) {
    if (mount) {
        mount->clear();
    }

    if (!mount ||
        !valid_named_pool_id(
            pool_id)) {
        return false;
    }

    auto& state =
        runtime_state();

    std::lock_guard<std::mutex>
        lock(state.mutex);

    const auto iterator =
        state.named_mounts.find(
            pool_id
        );

    if (iterator ==
        state.named_mounts.end()) {
        return false;
    }

    *mount =
        iterator->second;

    return true;
}

bool resolve_storage_pool_data_root_strict(
    const std::string& pool_id,
    std::filesystem::path* data_root
) {
    if (data_root) {
        data_root->clear();
    }

    if (!data_root) {
        return false;
    }

    auto& state =
        runtime_state();

    if (pool_id.empty() ||
        pool_id == "default") {
        std::lock_guard<std::mutex>
            lock(state.mutex);

        if (state.default_data_root.empty() ||
            !state.default_data_root.is_absolute()) {
            return false;
        }

        *data_root =
            state.default_data_root;

        return true;
    }

    if (!valid_named_pool_id(
            pool_id)) {
        return false;
    }

    std::filesystem::path mount;

    {
        std::lock_guard<std::mutex>
            lock(state.mutex);

        const auto iterator =
            state.named_mounts.find(
                pool_id
            );

        if (iterator ==
            state.named_mounts.end()) {
            // Security: never fall back from an unknown named pool
            // to the implicit/default data root.
            return false;
        }

        mount =
            iterator->second;
    }

    if (mount.empty() ||
        !mount.is_absolute()) {
        return false;
    }

    const std::filesystem::path resolved =
        (mount / "data").lexically_normal();

    if (resolved.empty() ||
        !resolved.is_absolute()) {
        return false;
    }

    *data_root =
        resolved;

    return true;
}

std::vector<StoragePoolRuntimeEntry>
storage_pool_runtime_snapshot() {
    std::vector<StoragePoolRuntimeEntry>
        output;

    auto& state =
        runtime_state();

    std::lock_guard<std::mutex>
        lock(state.mutex);

    output.reserve(
        state.named_mounts.size()
    );

    for (const auto& item :
         state.named_mounts) {
        StoragePoolRuntimeEntry entry;

        entry.pool_id =
            item.first;

        entry.mount =
            item.second;

        output.push_back(
            std::move(entry)
        );
    }

    return output;
}

} // namespace pqnas
