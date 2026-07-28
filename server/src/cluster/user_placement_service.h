#pragma once

#include <functional>
#include <map>
#include <string>

namespace pqnas {

class UsersRegistry;

namespace cluster {

class NodeRegistry;

using UserPlacementAuditEmit =
    std::function<void(
        const std::string& event,
        const std::string& outcome,
        const std::map<
            std::string,
            std::string
        >& fields
    )>;

struct UserPlacementServiceConfig {
    UsersRegistry* users = nullptr;
    NodeRegistry* nodes = nullptr;

    std::string users_path;
    UserPlacementAuditEmit audit_emit;
};

struct UserPlacementResult {
    bool ok = false;
    bool changed = false;

    std::string error_code;
    std::string previous_home_node;
    std::string home_node;
};

class UserPlacementService {
public:
    explicit UserPlacementService(
        UserPlacementServiceConfig config
    );

    UserPlacementResult assign(
        const std::string& fingerprint,
        const std::string& node_id,
        const std::string& actor_fingerprint
    );

private:
    bool config_valid() const;

    void audit_best_effort(
        const std::string& outcome,
        const std::string& fingerprint,
        const std::string& node_id,
        const std::string& actor_fingerprint,
        const std::string& reason_code,
        bool changed,
        const std::string& previous_home_node
    ) const;

    UserPlacementServiceConfig config_;
};

} // namespace cluster
} // namespace pqnas
