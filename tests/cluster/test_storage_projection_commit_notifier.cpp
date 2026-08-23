#include "users_registry.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

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

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-storage-projection-notifier-test-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(
            writable.data()
        );

    if (!result) {
        return {};
    }

    return fs::path(result);
}

pqnas::UserRec make_user() {
    pqnas::UserRec user;

    user.fingerprint =
        "notifier-user";

    user.name =
        "Notifier Test";

    user.role =
        "user";

    user.status =
        "disabled";

    user.storage_state =
        "unallocated";

    user.quota_bytes = 0;
    user.root_rel.clear();
    user.storage_pool_id.clear();

    user.home_node.clear();
    user.placement_policy_id.clear();

    return user;
}

} // namespace


int main() {
    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;

        fs::remove_all(
            directory,
            error
        );
    };

    const fs::path users_path =
        directory /
        "users.json";

    pqnas::UsersRegistry users;

    int notify_count = 0;
    bool callback_reentered_registry = false;

    std::vector<
        pqnas::StorageProjectionCommitInfo
    > notifications;

    users.set_storage_projection_commit_notifier(
        [&](const pqnas::StorageProjectionCommitInfo& info) {
            ++notify_count;

            notifications.push_back(
                info
            );

            // Deadlock guard: this succeeds only if the notifier runs outside
            // UsersRegistry::mu_. External callbacks must never run under the
            // registry mutation lock.
            if (users.get(
                    "notifier-user"
                ).has_value()) {
                callback_reentered_registry = true;
            }
        }
    );

    bool projection_changed = false;
    std::string error;

    // New persisted users establish the first authoritative projection.
    expect(
        users.upsert_persisted(
            make_user(),
            users_path.string(),
            &projection_changed,
            &error
        ),
        "initial user persistence succeeds: " +
            error
    );

    expect(
        projection_changed &&
        notify_count == 1 &&
        callback_reentered_registry &&
        notifications.size() == 1 &&
        notifications[0].fingerprint ==
            "notifier-user" &&
        notifications[0].previous_revision == 0 &&
        notifications[0].projection_revision == 1 &&
        notifications[0].previous_home_node.empty() &&
        notifications[0].home_node.empty(),
        "new projection commit reports exact revision transition outside registry lock"
    );

    // Profile-only changes are persisted but are invisible to Storage.
    {
        auto user =
            users.get(
                "notifier-user"
            );

        expect(
            user.has_value(),
            "user exists before profile-only update"
        );

        if (user) {
            user->name =
                "Profile-only change";

            projection_changed = true;
            error.clear();

            expect(
                users.upsert_persisted(
                    *user,
                    users_path.string(),
                    &projection_changed,
                    &error
                ),
                "profile-only update persists"
            );

            expect(
                !projection_changed &&
                notify_count == 1,
                "profile-only update does not notify"
            );
        }
    }

    // Disabled and revoked both project to enabled=false.
    {
        bool changed = false;
        error.clear();

        expect(
            users.set_status_persisted(
                "notifier-user",
                "revoked",
                users_path.string(),
                &changed,
                &error
            ),
            "disabled to revoked persists"
        );

        expect(
            changed &&
            notify_count == 1,
            "disabled to revoked does not notify"
        );
    }

    // revoked -> enabled changes Node authorization.
    {
        bool changed = false;

        expect(
            users.set_status_persisted(
                "notifier-user",
                "enabled",
                users_path.string(),
                &changed,
                &error
            ),
            "revoked to enabled persists"
        );

        expect(
            changed &&
            notify_count == 2,
            "enablement notifies"
        );
    }

    // enabled -> disabled revokes Node authorization.
    {
        bool changed = false;

        expect(
            users.set_status_persisted(
                "notifier-user",
                "disabled",
                users_path.string(),
                &changed,
                &error
            ),
            "enabled to disabled persists"
        );

        expect(
            changed &&
            notify_count == 3,
            "disablement notifies"
        );
    }

    // Allocation metadata is Node-visible.
    pqnas::UserStorageMetadataUpdate storage_update;
    storage_update.storage_state =
        "allocated";
    storage_update.quota_bytes =
        1024;
    storage_update.root_rel =
        "users/notifier-user";
    storage_update.storage_pool_id =
        "pool_01";
    storage_update.storage_set_at =
        "2026-08-17T01:00:00Z";
    storage_update.storage_set_by =
        "notifier-admin";

    {
        bool changed = false;
        projection_changed = false;

        expect(
            users.set_storage_metadata_persisted(
                "notifier-user",
                storage_update,
                users_path.string(),
                &changed,
                &projection_changed,
                &error
            ),
            "storage allocation persists"
        );

        expect(
            changed &&
            projection_changed &&
            notify_count == 4,
            "storage projection change notifies"
        );
    }

    // Audit metadata is persisted but excluded from the projection.
    {
        auto audit_update =
            storage_update;

        audit_update.storage_set_at =
            "2026-08-17T02:00:00Z";

        bool changed = false;
        projection_changed = true;

        expect(
            users.set_storage_metadata_persisted(
                "notifier-user",
                audit_update,
                users_path.string(),
                &changed,
                &projection_changed,
                &error
            ),
            "storage audit-only update persists"
        );

        expect(
            changed &&
            !projection_changed &&
            notify_count == 4,
            "storage audit-only update does not notify"
        );

        storage_update =
            audit_update;
    }

    // Stale migration CAS is a successful no-op.
    {
        bool expectation_matched = true;
        bool changed = true;
        projection_changed = true;

        expect(
            users.switch_storage_mapping_persisted_if_pool_current(
                "notifier-user",
                "wrong_pool",
                "pool_02",
                "migrated/notifier-user",
                "2026-08-17T03:00:00Z",
                "notifier-admin",
                users_path.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "stale migration CAS returns successful no-op"
        );

        expect(
            !expectation_matched &&
            !changed &&
            !projection_changed &&
            notify_count == 4,
            "stale migration CAS does not notify"
        );
    }

    // Matching migration changes the projected mapping.
    {
        bool expectation_matched = false;
        bool changed = false;
        projection_changed = false;

        expect(
            users.switch_storage_mapping_persisted_if_pool_current(
                "notifier-user",
                "pool_01",
                "pool_02",
                "migrated/notifier-user",
                "2026-08-17T03:00:00Z",
                "notifier-admin",
                users_path.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "matching migration CAS persists"
        );

        expect(
            expectation_matched &&
            changed &&
            projection_changed &&
            notify_count == 5,
            "matching migration projection change notifies"
        );
    }

    // An exact migration retry is idempotent.
    {
        bool expectation_matched = false;
        bool changed = true;
        projection_changed = true;

        expect(
            users.switch_storage_mapping_persisted_if_pool_current(
                "notifier-user",
                "pool_02",
                "pool_02",
                "migrated/notifier-user",
                "2026-08-17T03:00:00Z",
                "notifier-admin",
                users_path.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "identical migration retry succeeds"
        );

        expect(
            expectation_matched &&
            !changed &&
            !projection_changed &&
            notify_count == 5,
            "identical migration retry does not notify"
        );
    }

    // Placement is part of the Storage projection.
    {
        bool changed = false;

        expect(
            users.set_home_node_persisted(
                "notifier-user",
                "node-01",
                users_path.string(),
                &changed,
                &error
            ),
            "direct placement persists"
        );

        expect(
            changed &&
            notify_count == 6,
            "direct placement notifies"
        );
    }

    // Stale placement CAS is a successful no-op.
    {
        bool changed = true;
        bool expectation_matched = true;

        expect(
            users.set_home_node_persisted_if_current(
                "notifier-user",
                "node-99",
                "",
                "node-02",
                users_path.string(),
                &changed,
                &expectation_matched,
                &error
            ),
            "stale placement CAS succeeds as no-op"
        );

        expect(
            !changed &&
            !expectation_matched &&
            notify_count == 6,
            "stale placement CAS does not notify"
        );
    }

    // Matching placement CAS changes authorization location.
    {
        bool changed = false;
        bool expectation_matched = false;

        expect(
            users.set_home_node_persisted_if_current(
                "notifier-user",
                "node-01",
                "",
                "node-02",
                users_path.string(),
                &changed,
                &expectation_matched,
                &error
            ),
            "matching placement CAS persists"
        );

        expect(
            changed &&
            expectation_matched &&
            notify_count == 7,
            "matching placement CAS notifies"
        );
    }

    // Explicit placed -> unplaced mutation advances authorization revision
    // and wakes reconciliation only after durable persistence.
    {
        const auto before =
            users.get(
                "notifier-user"
            );

        const int notify_before =
            notify_count;

        bool changed = false;
        bool expectation_matched = false;
        error.clear();

        expect(
            users.clear_home_node_persisted_if_current(
                "notifier-user",
                "node-02",
                "",
                users_path.string(),
                &changed,
                &expectation_matched,
                &error
            ),
            "matching unplacement CAS persists"
        );

        const auto after =
            users.get(
                "notifier-user"
            );

        expect(
            before &&
            after &&
            changed &&
            expectation_matched &&
            after->home_node.empty() &&
            after->placement_policy_id ==
                before->placement_policy_id &&
            after->storage_projection_revision ==
                before->storage_projection_revision + 1 &&
            notify_count ==
                notify_before + 1,
            "durable unplacement advances revision, preserves intent and notifies"
        );
    }

    // Retrying the stale placed-state CAS after unplacement must be a no-op.
    {
        const int notify_before =
            notify_count;

        bool changed = true;
        bool expectation_matched = true;
        error.clear();

        expect(
            users.clear_home_node_persisted_if_current(
                "notifier-user",
                "node-02",
                "",
                users_path.string(),
                &changed,
                &expectation_matched,
                &error
            ),
            "stale unplacement CAS succeeds as no-op"
        );

        expect(
            !changed &&
            !expectation_matched &&
            notify_count ==
                notify_before,
            "stale unplacement CAS does not notify"
        );
    }


    // All successful projection mutations must form one monotonic audit
    // correlation chain. No-op/profile-only mutations above add no entries.
    expect(
        notifications.size() == 8,
        "all projection mutations report commit metadata"
    );

    for (std::size_t i = 0;
         i < notifications.size();
         ++i) {
        const auto& notification =
            notifications[i];

        expect(
            notification.fingerprint ==
                "notifier-user",
            "projection commit metadata identifies exact user"
        );

        expect(
            notification.projection_revision ==
                notification.previous_revision + 1,
            "projection commit metadata reports one revision advance"
        );

        if (i > 0) {
            expect(
                notification.previous_revision ==
                    notifications[i - 1].
                        projection_revision,
                "projection commit metadata forms monotonic revision chain"
            );
        }
    }

    expect(
        notifications.size() >= 8 &&
        notifications[5].previous_home_node.empty() &&
        notifications[5].home_node ==
            "node-01" &&
        notifications[6].previous_home_node ==
            "node-01" &&
        notifications[6].home_node ==
            "node-02" &&
        notifications[7].previous_home_node ==
            "node-02" &&
        notifications[7].home_node.empty(),
        "placement commit metadata reports exact home-node transitions"
    );

    // Failed persistence rolls back state and must never notify.
    {
        const auto before =
            users.get(
                "notifier-user"
            );

        const int notify_before =
            notify_count;

        bool changed = true;
        error.clear();

        // Security test fixture: an existing directory cannot serve as the
        // users.json replacement target, forcing persistence failure.
        expect(
            !users.set_status_persisted(
                "notifier-user",
                "enabled",
                directory.string(),
                &changed,
                &error
            ),
            "forced users.json persistence failure reports failure"
        );

        const auto after =
            users.get(
                "notifier-user"
            );

        expect(
            before &&
            after &&
            before->status ==
                after->status &&
            before->storage_projection_revision ==
                after->storage_projection_revision &&
            !changed &&
            notify_count ==
                notify_before,
            "failed persistence rolls back and does not notify"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " storage projection notifier test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage projection commit notifier tests passed\n";

    return 0;
}
