#pragma once

#include "node_registry.h"
#include "user_placement_policy.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {

class UserPlacementRecommendationApplyService;

struct UserPlacementPreviewUserSnapshot {
    std::string home_node;
    std::string placement_policy_id;
};

struct UserPlacementPreviewServiceConfig {
    std::function<std::optional<
        UserPlacementPreviewUserSnapshot
    >(
        const std::string& fingerprint
    )> get_user;

    std::function<
        std::vector<NodeRegistryRecord>()
    > node_snapshot;

    std::function<std::optional<
        UserPlacementPolicy
    >(
        const std::string& policy_id
    )> get_policy;

    std::function<std::unordered_map<
        std::string,
        std::size_t
    >()> assigned_user_counts;

    // Serializes preview against Director placement and control-plane admin
    // operations using the same coordination lock.
    std::shared_ptr<std::mutex>
        placement_mutex;
};

struct UserPlacementPreviewResult {
    // false means the preview request itself could not be evaluated.
    bool ok = false;

    // A successfully evaluated preview may still have no eligible target.
    bool recommendation_available = false;
    bool used_fallback = false;
    bool current_home_node_matches = false;

    std::string error_code;

    std::string placement_policy_id;
    std::string policy_source;
    std::string policy_mode;
    std::string policy_fallback;
    std::string selected_scope;

    std::string current_home_node;
    std::string recommended_home_node;
    std::string selection_reason;

    std::size_t considered_nodes = 0;
    std::size_t eligible_nodes = 0;
    std::size_t assigned_users = 0;

    std::uint64_t storage_total_bytes = 0;
    std::uint64_t storage_free_bytes = 0;

    // Aggregate bounded selector rejection categories only.
    std::map<std::string, std::size_t>
        rejected;
};

class UserPlacementPreviewService {
public:
    explicit UserPlacementPreviewService(
        UserPlacementPreviewServiceConfig
            config
    );

    // Read-only: never changes users, policies, nodes or persistent files.
    UserPlacementPreviewResult preview(
        const std::string& fingerprint
    ) const;

private:
    friend class
        UserPlacementRecommendationApplyService;

    bool config_valid() const;

    // Caller must already hold coordination_mutex(). This exists only so the
    // apply service can re-evaluate and persist under one TOCTOU boundary.
    UserPlacementPreviewResult
    preview_while_holding_coordination_lock(
        const std::string& fingerprint
    ) const;

    std::shared_ptr<std::mutex>
    coordination_mutex() const;

    UserPlacementPreviewServiceConfig
        config_;
};

} // namespace pqnas::cluster
