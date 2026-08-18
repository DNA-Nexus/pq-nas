#include "cluster/storage_user_session_policy.h"

#include "cluster/cluster_identity_validation.h"
#include "storage_metadata_validation.h"

#include <utility>

namespace pqnas::cluster {
namespace {

void clear_authorization_output(
    StorageUserSessionAuthorization* output
) noexcept {
    if (!output) {
        return;
    }

    output->fingerprint.clear();
    output->home_node.clear();
    output->projection_revision = 0;
    output->role.clear();
}

} // namespace

const char* storage_user_session_policy_result_code(
    StorageUserSessionPolicyResult result
) noexcept {
    switch (result) {
        case StorageUserSessionPolicyResult::Authorized:
            return "authorized";

        case StorageUserSessionPolicyResult::InvalidInput:
            return "invalid_input";

        case StorageUserSessionPolicyResult::ProjectionMissing:
            return "projection_missing";

        case StorageUserSessionPolicyResult::ProjectionInvalid:
            return "projection_invalid";

        case StorageUserSessionPolicyResult::Disabled:
            return "disabled";

        case StorageUserSessionPolicyResult::StorageUnallocated:
            return "storage_unallocated";

        case StorageUserSessionPolicyResult::WrongHomeNode:
            return "wrong_home_node";

        case StorageUserSessionPolicyResult::Unavailable:
            return "unavailable";
    }

    return "unavailable";
}

StorageUserSessionPolicy::StorageUserSessionPolicy(
    Config config
)
    : config_(std::move(config)) {
}

StorageUserSessionPolicyResult
StorageUserSessionPolicy::authorize(
    const std::string& fingerprint,
    StorageUserSessionAuthorization* output
) const noexcept {
    clear_authorization_output(output);

    if (!output ||
        !valid_cluster_node_id(
            config_.local_node_id
        ) ||
        !pqnas::valid_storage_user_fingerprint_hex(
            fingerprint
        ) ||
        !config_.lookup_projection) {
        return StorageUserSessionPolicyResult::
            InvalidInput;
    }

    try {
        // Security: fetch a fresh projection for every authorization decision.
        // A revoke/tombstone therefore invalidates an existing Storage session
        // on its next authenticated request rather than waiting for cookie TTL.
        const std::optional<StorageUserProjection>
            projection =
                config_.lookup_projection(
                    fingerprint
                );

        if (!projection.has_value()) {
            return StorageUserSessionPolicyResult::
                ProjectionMissing;
        }

        // Defense in depth: the persistent projection store already validates
        // these invariants, but fail closed if this policy is ever miswired to
        // another projection source.
        if (projection->fingerprint != fingerprint ||
            !pqnas::valid_storage_user_fingerprint_hex(
                projection->fingerprint
            ) ||
            !valid_cluster_node_id(
                projection->home_node
            ) ||
            projection->revision == 0) {
            return StorageUserSessionPolicyResult::
                ProjectionInvalid;
        }

        if (!projection->enabled) {
            return StorageUserSessionPolicyResult::
                Disabled;
        }

        if (!projection->storage_allocated) {
            return StorageUserSessionPolicyResult::
                StorageUnallocated;
        }

        if (projection->home_node !=
            config_.local_node_id) {
            return StorageUserSessionPolicyResult::
                WrongHomeNode;
        }

        output->fingerprint =
            projection->fingerprint;

        output->home_node =
            projection->home_node;

        output->projection_revision =
            projection->revision;

        // Security: Storage projection data deliberately carries no Director
        // role metadata, so Storage sessions can never become admin sessions.
        output->role =
            kStorageUserSessionRole;

        return StorageUserSessionPolicyResult::
            Authorized;
    } catch (...) {
        // Security: lookup/allocation failures must never preserve a previous
        // positive decision or degrade into authorization success.
        clear_authorization_output(output);

        return StorageUserSessionPolicyResult::
            Unavailable;
    }
}

} // namespace pqnas::cluster
