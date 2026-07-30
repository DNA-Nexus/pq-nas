#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>

namespace pqnas {

class UsersRegistry;

namespace cluster {

class UserPlacementPreviewService;

using UserPlacementRecommendationApplyAuditEmit =
    std::function<void(
        const std::string& event,
        const std::string& outcome,
        const std::map<
            std::string,
            std::string
        >& fields
    )>;

struct UserPlacementRecommendationApplyServiceConfig {
    UsersRegistry* users = nullptr;

    std::shared_ptr<
        UserPlacementPreviewService
    > preview_service;

    std::string users_path;

    UserPlacementRecommendationApplyAuditEmit
        audit_emit;
};

struct UserPlacementRecommendationApplyResult {
    bool ok = false;
    bool changed = false;

    std::string error_code;

    std::string previous_home_node;
    std::string home_node;
    std::string placement_policy_id;
};

class UserPlacementRecommendationApplyService {
public:
    explicit UserPlacementRecommendationApplyService(
        UserPlacementRecommendationApplyServiceConfig
            config
    );

    // Re-evaluates the recommendation under the same coordination lock used
    // for the final compare-and-set persistence.
    UserPlacementRecommendationApplyResult apply(
        const std::string& fingerprint,
        const std::string& expected_current_home_node,
        const std::string& expected_placement_policy_id,
        const std::string& expected_recommended_home_node,
        const std::string& actor_fingerprint
    );

private:
    bool config_valid() const;

    void audit_best_effort(
        const std::string& outcome,
        const std::string& fingerprint,
        const std::string& actor_fingerprint,
        const std::string& previous_home_node,
        const std::string& home_node,
        const std::string& placement_policy_id,
        const std::string& reason_code,
        bool changed
    ) const;

    UserPlacementRecommendationApplyServiceConfig
        config_;
};

} // namespace cluster
} // namespace pqnas
