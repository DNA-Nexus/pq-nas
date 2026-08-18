#include "cluster/storage_user_session_policy.h"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {

using pqnas::cluster::
    StorageUserProjection;

using pqnas::cluster::
    StorageUserSessionAuthorization;

using pqnas::cluster::
    StorageUserSessionPolicy;

using pqnas::cluster::
    StorageUserSessionPolicyResult;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

void expect_result(
    StorageUserSessionPolicyResult actual,
    StorageUserSessionPolicyResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                storage_user_session_policy_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                storage_user_session_policy_result_code(
                    actual
                )
    );
}

StorageUserProjection valid_projection(
    const std::string& fingerprint
) {
    StorageUserProjection projection;

    projection.fingerprint = fingerprint;
    projection.enabled = true;
    projection.storage_allocated = true;
    projection.quota_bytes = 1024;
    projection.root_rel = "users/example";
    projection.storage_pool_id = "";
    projection.home_node = "storage-01";
    projection.revision = 17;

    return projection;
}

} // namespace

int main() {
    const std::string fingerprint(
        128,
        'a'
    );

    std::optional<StorageUserProjection>
        current_projection;

    int lookup_calls = 0;

    StorageUserSessionPolicy::Config config;
    config.local_node_id = "storage-01";

    config.lookup_projection =
        [&](const std::string& candidate)
            -> std::optional<StorageUserProjection> {
            ++lookup_calls;

            if (!current_projection.has_value() ||
                current_projection->fingerprint !=
                    candidate) {
                return std::nullopt;
            }

            return current_projection;
        };

    StorageUserSessionPolicy policy(
        std::move(config)
    );

    StorageUserSessionAuthorization authorization;

    expect_result(
        policy.authorize(
            std::string(127, 'a'),
            &authorization
        ),
        StorageUserSessionPolicyResult::
            InvalidInput,
        "malformed fingerprint fails closed"
    );

    expect(
        lookup_calls == 0,
        "invalid fingerprint performs no projection lookup"
    );

    expect_result(
        policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            ProjectionMissing,
        "missing projection fails closed"
    );

    current_projection =
        valid_projection(fingerprint);

    current_projection->enabled = false;

    expect_result(
        policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            Disabled,
        "disabled projection fails closed"
    );

    current_projection =
        valid_projection(fingerprint);

    current_projection->
        storage_allocated = false;

    expect_result(
        policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            StorageUnallocated,
        "unallocated projection fails closed"
    );

    current_projection =
        valid_projection(fingerprint);

    current_projection->home_node =
        "storage-02";

    expect_result(
        policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            WrongHomeNode,
        "foreign home node fails closed"
    );

    current_projection =
        valid_projection(fingerprint);

    expect_result(
        policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            Authorized,
        "valid local projection authorizes"
    );

    expect(
        authorization.fingerprint ==
            fingerprint &&
        authorization.home_node ==
            "storage-01" &&
        authorization.projection_revision ==
            17 &&
        authorization.role == "user",
        "successful authorization returns minimal user identity"
    );

    // Security regression: authorization must not be cached. Replacing the
    // live rev17 projection with a rev18 revoke tombstone must invalidate the
    // same existing session identity on its next authorization check.
    StorageUserProjection tombstone =
        valid_projection(fingerprint);

    tombstone.revision = 18;
    tombstone.enabled = false;
    tombstone.storage_allocated = false;
    tombstone.quota_bytes = 0;
    tombstone.root_rel.clear();
    tombstone.storage_pool_id.clear();

    current_projection =
        std::move(tombstone);

    expect_result(
        policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            Disabled,
        "rev18 tombstone revokes prior session authorization"
    );

    expect(
        authorization.fingerprint.empty() &&
        authorization.home_node.empty() &&
        authorization.projection_revision == 0 &&
        authorization.role.empty(),
        "failed authorization clears previous positive output"
    );

    current_projection =
        valid_projection(fingerprint);

    current_projection->fingerprint =
        std::string(128, 'b');

    expect_result(
        policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            ProjectionMissing,
        "lookup does not alias a different fingerprint"
    );

    StorageUserSessionPolicy::Config
        invalid_projection_config;

    invalid_projection_config.local_node_id =
        "storage-01";

    invalid_projection_config.lookup_projection =
        [&](const std::string&)
            -> std::optional<StorageUserProjection> {
            StorageUserProjection bad =
                valid_projection(fingerprint);

            bad.revision = 0;
            return bad;
        };

    StorageUserSessionPolicy
        invalid_projection_policy(
            std::move(
                invalid_projection_config
            )
        );

    expect_result(
        invalid_projection_policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            ProjectionInvalid,
        "zero-revision projection fails closed"
    );

    StorageUserSessionPolicy::Config
        throwing_config;

    throwing_config.local_node_id =
        "storage-01";

    throwing_config.lookup_projection =
        [](const std::string&)
            -> std::optional<StorageUserProjection> {
            throw std::runtime_error(
                "synthetic lookup failure"
            );
        };

    StorageUserSessionPolicy
        throwing_policy(
            std::move(throwing_config)
        );

    authorization.fingerprint =
        fingerprint;
    authorization.home_node =
        "storage-01";
    authorization.projection_revision = 99;
    authorization.role = "user";

    expect_result(
        throwing_policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            Unavailable,
        "projection lookup exception fails closed"
    );

    expect(
        authorization.fingerprint.empty() &&
        authorization.home_node.empty() &&
        authorization.projection_revision == 0 &&
        authorization.role.empty(),
        "lookup failure clears stale authorization output"
    );

    StorageUserSessionPolicy::Config
        bad_node_config;

    bad_node_config.local_node_id =
        "../storage-01";

    bad_node_config.lookup_projection =
        [&](const std::string&)
            -> std::optional<StorageUserProjection> {
            return valid_projection(
                fingerprint
            );
        };

    StorageUserSessionPolicy
        bad_node_policy(
            std::move(bad_node_config)
        );

    expect_result(
        bad_node_policy.authorize(
            fingerprint,
            &authorization
        ),
        StorageUserSessionPolicyResult::
            InvalidInput,
        "invalid local node id fails closed"
    );

    expect_result(
        policy.authorize(
            fingerprint,
            nullptr
        ),
        StorageUserSessionPolicyResult::
            InvalidInput,
        "missing authorization output fails closed"
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " Storage user session policy test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage user session policy tests passed\n";

    return 0;
}
