#include "cluster/storage_user_projection_sync_service.h"

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
            "pqnas-projection-sync-service-XXXXXX"
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

pqnas::UserRec make_user(
    const std::string& fingerprint,
    const std::string& home_node
) {
    pqnas::UserRec user;

    user.fingerprint =
        fingerprint;

    user.name =
        "Projection Sync Service";

    user.role =
        "user";

    user.status =
        "enabled";

    user.storage_state =
        "allocated";

    user.quota_bytes =
        4096;

    user.root_rel =
        "users/example";

    user.storage_pool_id =
        "pool_01";

    user.home_node =
        home_node;

    return user;
}

pqnas::cluster::StorageUserProjectionSyncResult
success_result() {
    pqnas::cluster::StorageUserProjectionSyncResult result;

    result.code =
        pqnas::cluster::
            StorageUserProjectionSyncClientCode::
                Applied;

    result.http_status = 200;

    return result;
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

    const std::string fingerprint(
        128,
        'a'
    );

    pqnas::UsersRegistry users;

    bool projection_changed = false;
    std::string error;

    expect(
        users.upsert_persisted(
            make_user(
                fingerprint,
                "storage-02"
            ),
            users_path.string(),
            &projection_changed,
            &error
        ),
        "test user persisted"
    );

    std::vector<
        pqnas::cluster::StorageUserProjection
    > sent;

    bool fail_first = false;

    pqnas::cluster::
        StorageUserProjectionSyncServiceConfig
            config;

    config.users =
        &users;

    config.sync_projection =
        [&](const pqnas::cluster::
                StorageUserProjection& projection) {
            sent.push_back(projection);

            if (fail_first &&
                sent.size() == 1) {
                pqnas::cluster::
                    StorageUserProjectionSyncResult
                        failed;

                failed.code =
                    pqnas::cluster::
                        StorageUserProjectionSyncClientCode::
                            TransportFailed;

                return failed;
            }

            auto ok =
                success_result();

            ok.revision =
                projection.revision;

            return ok;
        };

    pqnas::cluster::
        StorageUserProjectionSyncService
            service(config);

    // Normal status/quota/storage mutation: synchronize only current home Node.
    {
        sent.clear();

        const auto result =
            service.sync_current(
                fingerprint
            );

        expect(
            result.ok &&
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncServiceCode::
                        Synced &&
            result.current_node_synced &&
            !result.previous_node_revoked &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-02" &&
            sent[0].enabled &&
            sent[0].storage_allocated,
            "current projection synchronizes to current home Node"
        );
    }

    // Placement transition: old Node gets a same-revision tombstone first.
    {
        sent.clear();
        fail_first = false;

        const auto result =
            service.sync_after_placement(
                fingerprint,
                "storage-01"
            );

        expect(
            result.ok &&
            result.previous_node_revoked &&
            result.current_node_synced &&
            sent.size() == 2,
            "placement transition performs two synchronization writes"
        );

        if (sent.size() == 2) {
            expect(
                sent[0].home_node ==
                    "storage-01" &&
                !sent[0].enabled &&
                !sent[0].storage_allocated &&
                sent[0].quota_bytes == 0 &&
                sent[0].root_rel.empty() &&
                sent[0].storage_pool_id.empty(),
                "previous Node receives disabled unallocated tombstone"
            );

            expect(
                sent[1].home_node ==
                    "storage-02" &&
                sent[1].enabled &&
                sent[1].storage_allocated,
                "current Node receives current Director projection"
            );

            expect(
                sent[0].revision ==
                    sent[1].revision,
                "old tombstone and current projection use same Director revision"
            );
        }
    }

    // Security: if old-Node revoke fails, do not send current projection.
    {
        sent.clear();
        fail_first = true;

        const auto result =
            service.sync_after_placement(
                fingerprint,
                "storage-01"
            );

        expect(
            !result.ok &&
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncServiceCode::
                        PreviousNodeRevokeFailed &&
            !result.current_node_synced &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-01",
            "failed old-Node revoke blocks new-Node synchronization"
        );

        fail_first = false;
    }

    // Idempotent same-node placement needs no tombstone.
    {
        sent.clear();

        const auto result =
            service.sync_after_placement(
                fingerprint,
                "storage-02"
            );

        expect(
            result.ok &&
            !result.previous_node_revoked &&
            result.current_node_synced &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-02",
            "same-node placement synchronizes current projection only"
        );
    }

    // First placement from empty previous Node needs only current projection.
    {
        sent.clear();

        const auto result =
            service.sync_after_placement(
                fingerprint,
                ""
            );

        expect(
            result.ok &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-02",
            "first placement has no previous-Node tombstone"
        );
    }

    // Invalid previous Node must fail before network activity.
    {
        sent.clear();

        const auto result =
            service.sync_after_placement(
                fingerprint,
                "../bad-node"
            );

        expect(
            !result.ok &&
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncServiceCode::
                        InvalidPreviousNode &&
            sent.empty(),
            "invalid previous Node fails closed"
        );
    }

    // Missing users fail before network activity.
    {
        sent.clear();

        const auto result =
            service.sync_current(
                std::string(
                    128,
                    'b'
                )
            );

        expect(
            !result.ok &&
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncServiceCode::
                        UserNotFound &&
            sent.empty(),
            "missing user is not synchronized"
        );
    }

    // Unplaced Director users are a normal no-op for current sync.
    {
        const std::string unplaced_fp(
            128,
            'c'
        );

        auto unplaced =
            make_user(
                unplaced_fp,
                ""
            );

        unplaced.storage_state =
            "unallocated";

        unplaced.quota_bytes = 0;
        unplaced.root_rel.clear();
        unplaced.storage_pool_id.clear();

        expect(
            users.upsert_persisted(
                unplaced,
                users_path.string(),
                &projection_changed,
                &error
            ),
            "unplaced test user persisted"
        );

        sent.clear();

        const auto result =
            service.sync_current(
                unplaced_fp
            );

        expect(
            result.ok &&
            result.code ==
                pqnas::cluster::
                    StorageUserProjectionSyncServiceCode::
                        NoPlacement &&
            sent.empty(),
            "unplaced user produces no network write"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection sync service test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection sync service tests passed\n";

    return 0;
}
