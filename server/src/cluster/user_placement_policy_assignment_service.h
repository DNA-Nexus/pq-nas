#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace pqnas {

class UsersRegistry;

namespace cluster {

class UserPlacementPolicyRegistry;

using UserPlacementPolicyAssignmentAuditEmit =
    std::function<void(
        const std::string& event,
        const std::string& outcome,
        const std::map<
            std::string,
            std::string
        >& fields
    )>;

struct UserPlacementPolicyAssignmentServiceConfig {
    UsersRegistry* users = nullptr;

    UserPlacementPolicyRegistry*
        policies = nullptr;

    // Shared with node administration and home-node placement. Future policy
    // administration must use the same mutex when changing referenced policy
    // state.
    std::shared_ptr<std::mutex>
        placement_mutex;

    std::string users_path;

    UserPlacementPolicyAssignmentAuditEmit
        audit_emit;
};

struct UserPlacementPolicyAssignmentResult {
    bool ok = false;
    bool changed = false;

    std::string error_code;
    std::string previous_policy_id;
    std::string placement_policy_id;
};

class UserPlacementPolicyAssignmentService {
public:
    explicit UserPlacementPolicyAssignmentService(
        UserPlacementPolicyAssignmentServiceConfig
            config
    );

    // Empty policy_id explicitly clears the user's policy override.
    //
    // This operation changes placement intent only. It never changes
    // home_node and never moves user data.
    UserPlacementPolicyAssignmentResult assign(
        const std::string& fingerprint,
        const std::string& policy_id,
        const std::string& actor_fingerprint
    );

private:
    bool config_valid() const;

    void audit_best_effort(
        const std::string& outcome,
        const std::string& fingerprint,
        const std::string& policy_id,
        const std::string& actor_fingerprint,
        const std::string& reason_code,
        bool changed,
        const std::string& previous_policy_id
    ) const;

    UserPlacementPolicyAssignmentServiceConfig
        config_;
};

} // namespace cluster
} // namespace pqnas
