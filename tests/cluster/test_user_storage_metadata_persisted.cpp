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
            "pqnas-user-storage-metadata-test-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(writable.data());

    if (!result) {
        return {};
    }

    return fs::path(result);
}

pqnas::UserRec make_user() {
    pqnas::UserRec user;

    user.fingerprint =
        "storage-metadata-user";

    user.name =
        "Storage Metadata Test";

    user.role =
        "user";

    user.status =
        "enabled";

    user.storage_state =
        "unallocated";

    user.quota_bytes = 0;

    return user;
}

pqnas::UserStorageMetadataUpdate
allocated_update(
    const std::string& pool_id,
    const std::string& set_at
) {
    pqnas::UserStorageMetadataUpdate update;

    update.storage_state =
        "allocated";

    update.quota_bytes =
        1024;

    update.root_rel =
        "users/storage-metadata-user";

    update.storage_pool_id =
        pool_id;

    update.storage_set_at =
        set_at;

    update.storage_set_by =
        "admin-user";

    return update;
}

} // namespace

int main() {
    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: could not create test directory\n";

        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(directory, error);
    };

    const fs::path users_path =
        directory / "users.json";

    pqnas::UsersRegistry users;

    bool projection_changed = false;
    std::string error;

    expect(
        users.upsert_persisted(
            make_user(),
            users_path.string(),
            &projection_changed,
            &error
        ),
        "initial persisted user succeeds: " +
            error
    );

    auto stored =
        users.get(
            "storage-metadata-user"
        );

    expect(
        stored &&
        stored->storage_projection_revision == 1,
        "initial user starts at revision 1"
    );

    // Allocation changes the Node-visible projection.
    {
        bool changed = false;
        projection_changed = false;

        const auto update =
            allocated_update(
                "pool_01",
                "2026-08-17T01:00:00Z"
            );

        expect(
            users.set_storage_metadata_persisted(
                "storage-metadata-user",
                update,
                users_path.string(),
                &changed,
                &projection_changed,
                &error
            ),
            "storage allocation persists"
        );

        stored =
            users.get(
                "storage-metadata-user"
            );

        expect(
            stored &&
            stored->storage_state == "allocated" &&
            stored->storage_pool_id == "pool_01" &&
            stored->quota_bytes == 1024 &&
            stored->storage_projection_revision == 2 &&
            changed &&
            projection_changed,
            "allocation advances projection revision"
        );
    }

    // Audit metadata can change without changing Node authorization.
    {
        bool changed = false;
        projection_changed = true;

        auto update =
            allocated_update(
                "pool_01",
                "2026-08-17T02:00:00Z"
            );

        update.storage_set_by =
            "other-admin";

        expect(
            users.set_storage_metadata_persisted(
                "storage-metadata-user",
                update,
                users_path.string(),
                &changed,
                &projection_changed,
                &error
            ),
            "storage audit metadata update persists"
        );

        stored =
            users.get(
                "storage-metadata-user"
            );

        expect(
            stored &&
            stored->storage_projection_revision == 2 &&
            changed &&
            !projection_changed,
            "audit-only update leaves projection revision stable"
        );
    }

    // Stale pool expectation is a successful no-op.
    {
        bool expectation_matched = true;
        bool changed = true;
        projection_changed = true;

        expect(
            users.switch_storage_mapping_persisted_if_pool_current(
                "storage-metadata-user",
                "wrong_pool",
                "pool_02",
                "migrated/storage-metadata-user",
                "2026-08-17T03:00:00Z",
                "migration-admin",
                users_path.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "stale migration CAS is successful no-op"
        );

        stored =
            users.get(
                "storage-metadata-user"
            );

        expect(
            stored &&
            stored->storage_pool_id == "pool_01" &&
            stored->quota_bytes == 1024 &&
            stored->storage_projection_revision == 2 &&
            !expectation_matched &&
            !changed &&
            !projection_changed,
            "stale migration CAS cannot overwrite metadata"
        );
    }

    // Matching migration CAS switches mapping but preserves quota/state.
    {
        bool expectation_matched = false;
        bool changed = false;
        projection_changed = false;

        expect(
            users.switch_storage_mapping_persisted_if_pool_current(
                "storage-metadata-user",
                "pool_01",
                "pool_02",
                "migrated/storage-metadata-user",
                "2026-08-17T04:00:00Z",
                "migration-admin",
                users_path.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "matching migration CAS persists"
        );

        stored =
            users.get(
                "storage-metadata-user"
            );

        expect(
            stored &&
            stored->storage_pool_id == "pool_02" &&
            stored->root_rel ==
                "migrated/storage-metadata-user" &&
            stored->storage_state == "allocated" &&
            stored->quota_bytes == 1024 &&
            stored->storage_projection_revision == 3 &&
            expectation_matched &&
            changed &&
            projection_changed,
            "migration switches mapping and preserves allocation/quota"
        );
    }

    // Simulate a concurrent quota update after migration planning.
    //
    // The mapping CAS must use current locked quota/state rather than stale
    // values from the worker that planned the migration.
    {
        auto quota_update =
            allocated_update(
                "pool_02",
                "2026-08-17T04:30:00Z"
            );

        quota_update.quota_bytes =
            4096;

        quota_update.root_rel =
            "migrated/storage-metadata-user";

        bool changed = false;
        bool quota_projection_changed = false;

        expect(
            users.set_storage_metadata_persisted(
                "storage-metadata-user",
                quota_update,
                users_path.string(),
                &changed,
                &quota_projection_changed,
                &error
            ),
            "concurrent quota update persists"
        );

        stored =
            users.get(
                "storage-metadata-user"
            );

        expect(
            stored &&
            stored->quota_bytes == 4096 &&
            stored->storage_projection_revision == 4,
            "quota update advances revision before next migration"
        );

        bool expectation_matched = false;
        changed = false;
        projection_changed = false;

        expect(
            users.switch_storage_mapping_persisted_if_pool_current(
                "storage-metadata-user",
                "pool_02",
                "pool_03",
                "second-migration/storage-metadata-user",
                "2026-08-17T05:00:00Z",
                "migration-admin",
                users_path.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "migration after concurrent quota update persists"
        );

        stored =
            users.get(
                "storage-metadata-user"
            );

        expect(
            stored &&
            stored->storage_pool_id == "pool_03" &&
            stored->quota_bytes == 4096 &&
            stored->storage_state == "allocated" &&
            stored->storage_projection_revision == 5,
            "mapping CAS preserves current quota/state"
        );
    }

    // Exact mapping/audit retry is idempotent.
    {
        bool expectation_matched = false;
        bool changed = true;
        projection_changed = true;

        expect(
            users.switch_storage_mapping_persisted_if_pool_current(
                "storage-metadata-user",
                "pool_03",
                "pool_03",
                "second-migration/storage-metadata-user",
                "2026-08-17T05:00:00Z",
                "migration-admin",
                users_path.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "identical migration CAS retry succeeds"
        );

        stored =
            users.get(
                "storage-metadata-user"
            );

        expect(
            stored &&
            stored->storage_projection_revision == 5 &&
            expectation_matched &&
            !changed &&
            !projection_changed,
            "identical migration retry is revision-stable"
        );
    }

    // Persistence failure rolls mapping and revision back together.
    {
        const auto before =
            users.get(
                "storage-metadata-user"
            );

        bool expectation_matched = false;
        bool changed = true;
        projection_changed = true;

        error.clear();

        expect(
            !users.switch_storage_mapping_persisted_if_pool_current(
                "storage-metadata-user",
                "pool_03",
                "pool_04",
                "failed/storage-metadata-user",
                "2026-08-17T06:00:00Z",
                "migration-admin",
                directory.string(),
                &expectation_matched,
                &changed,
                &projection_changed,
                &error
            ),
            "failed mapping persistence reports failure"
        );

        const auto after =
            users.get(
                "storage-metadata-user"
            );

        expect(
            before &&
            after &&
            expectation_matched &&
            after->storage_pool_id ==
                before->storage_pool_id &&
            after->root_rel ==
                before->root_rel &&
            after->quota_bytes ==
                before->quota_bytes &&
            after->storage_projection_revision ==
                before->storage_projection_revision &&
            !changed &&
            !projection_changed,
            "failed mapping persistence rolls state and revision back"
        );
    }

    // Successful state survives reload.
    {
        pqnas::UsersRegistry reloaded;

        expect(
            reloaded.load(
                users_path.string()
            ),
            "storage metadata users.json reloads"
        );

        const auto reloaded_user =
            reloaded.get(
                "storage-metadata-user"
            );

        expect(
            reloaded_user &&
            reloaded_user->storage_pool_id == "pool_03" &&
            reloaded_user->quota_bytes == 4096 &&
            reloaded_user->storage_projection_revision == 5,
            "mapping/quota/revision survive reload"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " persisted storage metadata test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: persisted storage metadata tests passed\n";

    return 0;
}
