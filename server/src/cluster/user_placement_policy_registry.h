#pragma once

#include "user_placement_policy.h"

#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {

inline constexpr int
    kUserPlacementPolicyRegistrySchemaVersion = 1;

class UserPlacementPolicyRegistry {
public:
    // Missing file means a valid empty registry. Malformed or unsafe input
    // fails without replacing the current in-memory snapshot.
    bool load(
        const std::string& path,
        std::string* error
    );

    std::optional<UserPlacementPolicy> get(
        const std::string& policy_id
    ) const;

    // Deterministically sorted by policy ID.
    std::vector<UserPlacementPolicy>
    snapshot() const;

    // Write-through persistence: memory changes only after the complete
    // candidate snapshot has been atomically committed.
    bool upsert(
        const UserPlacementPolicy& policy,
        std::string* error
    );

    bool erase(
        const std::string& policy_id,
        std::string* error
    );

private:
    bool persist_candidate_locked(
        std::unordered_map<
            std::string,
            UserPlacementPolicy
        >&& candidate,
        std::string* error
    );

    bool ensure_durable_locked(
        std::string* error
    );

    mutable std::mutex mutex_;
    std::filesystem::path path_;

    std::unordered_map<
        std::string,
        UserPlacementPolicy
    > policies_;

    // A rename may already have changed the visible authoritative snapshot
    // even when the following parent-directory fsync fails. Exact retries
    // must re-establish that missing durability boundary.
    bool durability_uncertain_ = false;
};

} // namespace pqnas::cluster
