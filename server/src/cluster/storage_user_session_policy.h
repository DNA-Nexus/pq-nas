#pragma once

#include "cluster/storage_user_projection_store.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace pqnas::cluster {

inline constexpr const char*
    kStorageUserSessionRole = "user";

// Security: Storage Nodes never infer Director admin privileges. A Storage
// session represents only an enabled user authorization projection.
enum class StorageUserSessionPolicyResult {
    Authorized,
    InvalidInput,
    ProjectionMissing,
    ProjectionInvalid,
    Disabled,
    StorageUnallocated,
    WrongHomeNode,
    Unavailable
};

const char* storage_user_session_policy_result_code(
    StorageUserSessionPolicyResult result
) noexcept;

struct StorageUserSessionAuthorization {
    std::string fingerprint;
    std::string home_node;

    std::uint64_t projection_revision = 0;

    std::string role;
};

class StorageUserSessionPolicy {
public:
    using ProjectionLookup =
        std::function<
            std::optional<StorageUserProjection>(
                const std::string&
            )
        >;

    struct Config {
        std::string local_node_id;
        ProjectionLookup lookup_projection;
    };

    explicit StorageUserSessionPolicy(
        Config config
    );

    // Security:
    // - authorization is evaluated from the current Storage projection on
    //   every call; no positive authorization is cached here
    // - missing/invalid/disabled/unallocated/wrong-node state fails closed
    // - successful Storage sessions are always ordinary users, never admins
    StorageUserSessionPolicyResult authorize(
        const std::string& fingerprint,
        StorageUserSessionAuthorization* output
    ) const noexcept;

private:
    Config config_;
};

} // namespace pqnas::cluster
