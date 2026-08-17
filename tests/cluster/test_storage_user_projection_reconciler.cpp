#include "cluster/storage_user_projection_reconciler.h"

#include "cluster/storage_user_projection_sync_state_store.h"
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
            "pqnas-projection-reconciler-XXXXXX"
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
    char fingerprint_character,
    const std::string& home_node
) {
    pqnas::UserRec user;

    user.fingerprint =
        std::string(
            128,
            fingerprint_character
        );

    user.name =
        "projection reconciler";

    user.role =
        "user";

    user.status =
        "enabled";

    user.storage_state =
        "unallocated";

    user.quota_bytes = 0;
    user.root_rel.clear();
    user.storage_pool_id.clear();

    user.home_node =
        home_node;

    return user;
}

pqnas::cluster::
StorageUserProjectionSyncResult
sync_success(
    const pqnas::cluster::
        StorageUserProjection& projection
) {
    pqnas::cluster::
        StorageUserProjectionSyncResult result;

    result.code =
        pqnas::cluster::
            StorageUserProjectionSyncClientCode::
                Applied;

    result.http_status = 200;
    result.revision =
        projection.revision;

    return result;
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
        directory / "users.json";

    const fs::path state_path =
        directory /
        "storage_projection_sync_state.json";

    pqnas::UsersRegistry users;

    pqnas::cluster::
        StorageUserProjectionSyncStateStore
            state_store;

    std::string error;

    expect(
        state_store.load(
            state_path.string(),
            &error
        ),
        "sync-state store loads"
    );

    bool projection_changed = false;

    const auto placed =
        make_user(
            'a',
            "storage-01"
        );

    expect(
        users.upsert_persisted(
            placed,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "placed user persisted"
    );

    const auto unplaced =
        make_user(
            'b',
            ""
        );

    expect(
        users.upsert_persisted(
            unplaced,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "unplaced user persisted"
    );

    std::vector<
        pqnas::cluster::
            StorageUserProjection
    > sent;

    bool fail_next = false;

    pqnas::cluster::
        StorageUserProjectionReconcilerConfig
            config;

    config.users =
        &users;

    config.state_store =
        &state_store;

    config.sync_projection =
        [&](const pqnas::cluster::
                StorageUserProjection& projection) {
            sent.push_back(
                projection
            );

            if (fail_next) {
                fail_next = false;

                pqnas::cluster::
                    StorageUserProjectionSyncResult
                        failed;

                failed.code =
                    pqnas::cluster::
                        StorageUserProjectionSyncClientCode::
                            TransportFailed;

                return failed;
            }

            return sync_success(
                projection
            );
        };

    pqnas::cluster::
        StorageUserProjectionReconciler
            reconciler(config);

    // First reconciliation sends the current placed projection and records the
    // exact Node/revision that accepted it.
    {
        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.synced_current == 1 &&
            result.no_placement == 1 &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-01" &&
            state.has_value() &&
            state->home_node ==
                "storage-01" &&
            state->revision ==
                sent[0].revision,
            "initial reconciliation synchronizes exact current projection"
        );
    }

    // No Director mutation means no network traffic.
    {
        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        expect(
            result.ok &&
            result.already_synced == 1 &&
            result.no_placement == 1 &&
            sent.empty(),
            "already-confirmed revision performs no network write"
        );
    }

    // Projection semantic change on the same Node advances revision and sends
    // only the new current projection.
    {
        bool changed = false;

        expect(
            users.set_status_persisted(
                placed.fingerprint,
                "disabled",
                users_path.string(),
                &changed,
                &error
            ) &&
            changed,
            "status mutation persisted"
        );

        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.synced_current == 1 &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-01" &&
            !sent[0].enabled &&
            state.has_value() &&
            state->revision ==
                sent[0].revision,
            "same-Node newer revision synchronizes current projection"
        );
    }

    const auto before_move_state =
        state_store.get(
            placed.fingerprint
        );

    expect(
        before_move_state.has_value(),
        "pre-move state exists"
    );

    // Placement change: failed old-Node revoke blocks the new Node and does not
    // advance the persistent confirmed state.
    {
        bool changed = false;

        expect(
            users.set_home_node_persisted(
                placed.fingerprint,
                "storage-02",
                users_path.string(),
                &changed,
                &error
            ) &&
            changed,
            "placement mutation persisted"
        );

        sent.clear();
        fail_next = true;

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            !result.ok &&
            result.failed == 1 &&
            sent.size() == 1 &&
            sent[0].home_node ==
                "storage-01" &&
            !sent[0].enabled &&
            !sent[0].storage_allocated &&
            state.has_value() &&
            before_move_state.has_value() &&
            state->home_node ==
                before_move_state->home_node &&
            state->revision ==
                before_move_state->revision,
            "failed previous-Node revoke blocks move and preserves state"
        );
    }

    // Retry: previous Node tombstone first, then current Node projection, then
    // persistent state advances to the new Node/revision.
    {
        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        const auto state =
            state_store.get(
                placed.fingerprint
            );

        expect(
            result.ok &&
            result.moved == 1 &&
            sent.size() == 2 &&
            sent[0].home_node ==
                "storage-01" &&
            !sent[0].enabled &&
            !sent[0].storage_allocated &&
            sent[1].home_node ==
                "storage-02" &&
            sent[0].revision ==
                sent[1].revision &&
            state.has_value() &&
            state->home_node ==
                "storage-02" &&
            state->revision ==
                sent[1].revision,
            "placement retry revokes old Node before synchronizing new Node"
        );
    }

    // Persistent state newer than Director is a fail-closed rollback signal.
    {
        const auto current =
            state_store.get(
                placed.fingerprint
            );

        expect(
            current.has_value(),
            "current state available for stale-state test"
        );

        if (current.has_value()) {
            pqnas::cluster::
                StorageUserProjectionSyncState
                    future = *current;

            future.revision += 10;

            expect(
                state_store.mark_synced(
                    future,
                    &error
                ) ==
                    pqnas::cluster::
                        StorageUserProjectionSyncStateStoreResult::
                            Applied,
                "future sync-state fixture persisted"
            );
        }

        sent.clear();

        const auto result =
            reconciler.reconcile_once();

        expect(
            !result.ok &&
            result.stale_states == 1 &&
            sent.empty(),
            "newer confirmed state prevents Director revision rollback"
        );
    }

    // Same revision on a different Node is an invariant conflict and must not
    // trigger network traffic.
    {
        const fs::path conflict_state_path =
            directory /
            "conflict_state.json";

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                conflict_store;

        expect(
            conflict_store.load(
                conflict_state_path.string(),
                &error
            ),
            "conflict state store loads"
        );

        const auto conflict_user =
            make_user(
                'c',
                "storage-02"
            );

        pqnas::UsersRegistry
            conflict_users;

        const fs::path conflict_users_path =
            directory /
            "conflict_users.json";

        expect(
            conflict_users.upsert_persisted(
                conflict_user,
                conflict_users_path.string(),
                &projection_changed,
                &error
            ),
            "conflict user persisted"
        );

        const auto persisted_conflict_user =
            conflict_users.get(
                conflict_user.fingerprint
            );

        expect(
            persisted_conflict_user.has_value(),
            "conflict user readable"
        );

        if (persisted_conflict_user.has_value()) {
            pqnas::cluster::
                StorageUserProjectionSyncState
                    conflicting_state;

            conflicting_state.fingerprint =
                conflict_user.fingerprint;

            conflicting_state.home_node =
                "storage-01";

            conflicting_state.revision =
                persisted_conflict_user->
                    storage_projection_revision;

            expect(
                conflict_store.mark_synced(
                    conflicting_state,
                    &error
                ) ==
                    pqnas::cluster::
                        StorageUserProjectionSyncStateStoreResult::
                            Applied,
                "same-revision different-Node fixture persisted"
            );
        }

        std::size_t network_calls = 0;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                conflict_config;

        conflict_config.users =
            &conflict_users;

        conflict_config.state_store =
            &conflict_store;

        conflict_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                ++network_calls;

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                conflict_reconciler(
                    conflict_config
                );

        const auto result =
            conflict_reconciler.reconcile_once();

        expect(
            !result.ok &&
            result.state_conflicts == 1 &&
            network_calls == 0,
            "same revision cannot silently migrate between Nodes"
        );
    }

    // State with no Director user is retained and reported, never erased.
    {
        const fs::path orphan_state_path =
            directory /
            "orphan_state.json";

        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                orphan_store;

        expect(
            orphan_store.load(
                orphan_state_path.string(),
                &error
            ),
            "orphan state store loads"
        );

        pqnas::cluster::
            StorageUserProjectionSyncState
                orphan;

        orphan.fingerprint =
            std::string(
                128,
                'd'
            );

        orphan.home_node =
            "storage-01";

        orphan.revision = 4;

        expect(
            orphan_store.mark_synced(
                orphan,
                &error
            ) ==
                pqnas::cluster::
                    StorageUserProjectionSyncStateStoreResult::
                        Applied,
            "orphan fixture persisted"
        );

        pqnas::UsersRegistry
            empty_users;

        std::size_t network_calls = 0;

        pqnas::cluster::
            StorageUserProjectionReconcilerConfig
                orphan_config;

        orphan_config.users =
            &empty_users;

        orphan_config.state_store =
            &orphan_store;

        orphan_config.sync_projection =
            [&](const pqnas::cluster::
                    StorageUserProjection& projection) {
                ++network_calls;

                return sync_success(
                    projection
                );
            };

        pqnas::cluster::
            StorageUserProjectionReconciler
                orphan_reconciler(
                    orphan_config
                );

        const auto result =
            orphan_reconciler.reconcile_once();

        expect(
            !result.ok &&
            result.orphaned_states == 1 &&
            network_calls == 0 &&
            orphan_store.get(
                orphan.fingerprint
            ).has_value(),
            "orphan state fails closed and remains persisted"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection reconciler test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection reconciler tests passed\n";

    return 0;
}
