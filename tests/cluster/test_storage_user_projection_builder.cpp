#include "cluster/storage_user_projection_builder.h"

#include "users_registry.h"

#include <iostream>
#include <string>

namespace {

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

pqnas::UserRec base_user() {
    pqnas::UserRec user;

    user.fingerprint =
        std::string(128, 'a');

    user.name =
        "Must not be projected";

    user.role =
        "admin";

    user.status =
        "enabled";

    user.home_node =
        "storage-01";

    user.placement_policy_id =
        "director-policy";

    user.storage_projection_revision =
        17;

    return user;
}

} // namespace

int main() {
    std::string error;

    // Allocated user projects exactly the Storage-visible fields.
    {
        pqnas::UserRec user =
            base_user();

        user.storage_state =
            "allocated";

        user.quota_bytes =
            123456;

        user.root_rel =
            "users/example";

        user.storage_pool_id =
            "pool_01";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "allocated projection builds: " +
                error
        );

        expect(
            projection.fingerprint ==
                user.fingerprint &&
            projection.enabled &&
            projection.storage_allocated &&
            projection.quota_bytes == 123456 &&
            projection.root_rel ==
                "users/example" &&
            projection.storage_pool_id ==
                "pool_01" &&
            projection.home_node ==
                "storage-01" &&
            projection.revision == 17,
            "allocated projection contains exact Node-visible metadata"
        );
    }

    // Non-enabled Director states collapse to enabled=false.
    {
        pqnas::UserRec user =
            base_user();

        user.status =
            "revoked";

        user.storage_state =
            "allocated";

        user.root_rel =
            "users/example";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "revoked projection builds"
        );

        expect(
            !projection.enabled,
            "revoked Director user projects enabled=false"
        );
    }

    // Unallocated projection must discard stale local physical metadata.
    {
        pqnas::UserRec user =
            base_user();

        user.storage_state =
            "unallocated";

        user.quota_bytes =
            999999;

        user.root_rel =
            "stale/local/root";

        user.storage_pool_id =
            "pool_01";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "unallocated projection builds"
        );

        expect(
            !projection.storage_allocated &&
            projection.quota_bytes == 0 &&
            projection.root_rel.empty() &&
            projection.storage_pool_id.empty(),
            "unallocated projection is canonical and carries no stale mapping"
        );
    }

    // Director placement is mandatory for Node synchronization.
    {
        pqnas::UserRec user =
            base_user();

        user.home_node.clear();
        user.storage_state =
            "unallocated";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            !pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "missing home node fails closed"
        );
    }

    // Legacy/unversioned state must not be sent to Storage.
    {
        pqnas::UserRec user =
            base_user();

        user.storage_state =
            "unallocated";

        user.storage_projection_revision =
            0;

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            !pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "revision zero fails closed"
        );
    }

    // Projection identity must use the canonical SHA3-512 hex form.
    {
        pqnas::UserRec user =
            base_user();

        user.fingerprint =
            "abc";

        user.storage_state =
            "unallocated";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            !pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "invalid fingerprint fails closed"
        );
    }

    // Invalid/noncanonical allocated paths must not cross the trust boundary.
    {
        pqnas::UserRec user =
            base_user();

        user.storage_state =
            "allocated";

        user.root_rel =
            "users/foo/../bar";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            !pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "noncanonical allocated root fails closed"
        );
    }

    // Unknown storage-state strings must not silently become unallocated.
    {
        pqnas::UserRec user =
            base_user();

        user.storage_state =
            "mystery-state";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            !pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "invalid storage state fails closed"
        );
    }

    // Managed pool syntax remains identical to Node-side validation.
    {
        pqnas::UserRec user =
            base_user();

        user.storage_state =
            "allocated";

        user.root_rel =
            "users/example";

        user.storage_pool_id =
            "BAD-POOL";

        pqnas::cluster::StorageUserProjection
            projection;

        expect(
            !pqnas::cluster::
                build_storage_user_projection(
                    user,
                    &projection,
                    &error
                ),
            "invalid pool ID fails closed"
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " storage projection builder test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection builder tests passed\n";

    return 0;
}
