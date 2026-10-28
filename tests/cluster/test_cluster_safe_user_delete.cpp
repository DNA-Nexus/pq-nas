#include "cluster/cluster_safe_user_delete.h"
#include "cluster/storage_user_projection_sync_state_store.h"
#include "cluster/storage_user_projection_transition_store.h"
#include "users_registry.h"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
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
            "pqnas-cluster-safe-delete-XXXXXX"
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


pqnas::UserRec make_placed_user() {
    pqnas::UserRec user;

    user.fingerprint =
        std::string(
            128,
            'a'
        );

    user.name =
        "RT-004 Director delete test";

    user.role =
        "user";

    user.status =
        "enabled";

    user.group =
        "External";

    user.storage_state =
        "allocated";

    user.home_node =
        "storage-01";

    return user;
}


pqnas::UserRec make_sentinel_user() {
    pqnas::UserRec user;

    user.fingerprint =
        std::string(
            128,
            'b'
        );

    user.name =
        "RT-004 sentinel";

    user.role =
        "user";

    user.status =
        "enabled";

    user.storage_state =
        "unallocated";

    user.home_node =
        "";

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

    const fs::path sync_state_path =
        directory /
        "storage_projection_sync_state.json";

    const fs::path transition_state_path =
        directory /
        "storage_projection_transition_journal.json";


    pqnas::UsersRegistry users;

    pqnas::cluster::
        StorageUserProjectionSyncStateStore
            state_store;

    pqnas::cluster::
        StorageUserProjectionTransitionStore
            transition_store;

    const auto transition_mutex =
        std::make_shared<std::mutex>();

    std::string error;


    // ---------------------------------------------------------------------
    // Bootstrap an empty confirmed-projection store.
    // ---------------------------------------------------------------------

    expect(
        state_store.load(
            sync_state_path.string(),
            &error
        ),
        "empty confirmed projection state loads: " +
            error
    );

    error.clear();

    expect(
        transition_store.load(
            transition_state_path.string(),
            &error
        ),
        "empty pending projection journal loads: " +
            error
    );


    // ---------------------------------------------------------------------
    // Persist one user as placed on storage-01.
    //
    // upsert_persisted() assigns the initial positive Director-owned
    // storage_projection_revision.
    // ---------------------------------------------------------------------

    bool projection_changed = false;

    const pqnas::UserRec sentinel =
        make_sentinel_user();

    error.clear();

    expect(
        users.upsert_persisted(
            sentinel,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "sentinel user persists: " +
            error
    );

    const pqnas::UserRec initial =
        make_placed_user();

    error.clear();

    expect(
        users.upsert_persisted(
            initial,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "placed user persists: " +
            error
    );

    const auto placed =
        users.get(
            initial.fingerprint
        );

    expect(
        placed.has_value(),
        "placed user remains readable"
    );

    expect(
        placed.has_value() &&
        !placed->home_node.empty(),
        "test user starts placed"
    );

    expect(
        placed.has_value() &&
        placed->storage_projection_revision > 0,
        "placed user receives positive projection revision"
    );

    std::uint64_t confirmed_revision = 0;

    if (placed.has_value()) {
        confirmed_revision =
            placed->storage_projection_revision;
    }


    // Record the projection known to have reached storage-01.
    if (confirmed_revision > 0) {
        pqnas::cluster::
            StorageUserProjectionSyncState
                confirmed;

        confirmed.fingerprint =
            initial.fingerprint;

        confirmed.home_node =
            "storage-01";

        confirmed.revision =
            confirmed_revision;

        error.clear();

        expect(
            state_store.mark_synced(
                confirmed,
                &error
            ) ==
                pqnas::cluster::
                    StorageUserProjectionSyncStateStoreResult::
                        Applied,
            "confirmed Storage projection persists: " +
                error
        );
    }


    // ---------------------------------------------------------------------
    // RT-004 case 1:
    //
    // A user with home_node is still placed. Physical Director deletion must
    // fail closed before examining whether revoke reconciliation can proceed.
    // ---------------------------------------------------------------------

    {
        bool erased = true;
        bool placement_blocked = false;
        bool cluster_revoke_pending = true;

        error.clear();

        const bool ok =
            pqnas::cluster::
                erase_user_persisted_cluster_safe(
                    users,
                    state_store,
                    transition_store,
                    transition_mutex,
                    users_path.string(),
                    initial.fingerprint,
                    &erased,
                    &placement_blocked,
                    &cluster_revoke_pending,
                    &error
                );

        expect(
            ok &&
            !erased &&
            placement_blocked &&
            !cluster_revoke_pending &&
            users.exists(
                initial.fingerprint
            ),
            "placed user deletion is blocked without mutation"
        );
    }


    // ---------------------------------------------------------------------
    // Persist the Director-side unplacement.
    //
    // This advances the authoritative projection revision, while the confirmed
    // state deliberately remains at the old storage-01 revision until revoke
    // reconciliation succeeds.
    // ---------------------------------------------------------------------

    bool placement_changed = false;
    bool expectation_matched = false;

    error.clear();

    expect(
        users.clear_home_node_persisted_if_current(
            initial.fingerprint,
            "storage-01",
            "",
            users_path.string(),
            &placement_changed,
            &expectation_matched,
            &error
        ) &&
        expectation_matched &&
        placement_changed,
        "Director unplacement persists: " +
            error
    );

    const auto unplaced =
        users.get(
            initial.fingerprint
        );

    expect(
        unplaced.has_value() &&
        unplaced->home_node.empty(),
        "Director user is now unplaced"
    );

    expect(
        unplaced.has_value() &&
        unplaced->storage_projection_revision >
            confirmed_revision,
        "unplacement advances authoritative projection revision"
    );


    // ---------------------------------------------------------------------
    // RT-004 case 2:
    //
    // users.json now says unplaced, but durable confirmed state still proves
    // storage-01 may authorize the fingerprint. Physical deletion must remain
    // blocked until that confirmed state disappears.
    // ---------------------------------------------------------------------

    {
        bool erased = true;
        bool placement_blocked = true;
        bool cluster_revoke_pending = false;

        error.clear();

        const bool ok =
            pqnas::cluster::
                erase_user_persisted_cluster_safe(
                    users,
                    state_store,
                    transition_store,
                    transition_mutex,
                    users_path.string(),
                    initial.fingerprint,
                    &erased,
                    &placement_blocked,
                    &cluster_revoke_pending,
                    &error
                );

        expect(
            ok &&
            !erased &&
            !placement_blocked &&
            cluster_revoke_pending &&
            users.exists(
                initial.fingerprint
            ),
            "confirmed Storage authorization blocks physical deletion"
        );

        expect(
            state_store.get(
                initial.fingerprint
            ).has_value(),
            "confirmed Storage state remains while revoke is pending"
        );
    }


    // ---------------------------------------------------------------------
    // Simulate successful Storage revoke reconciliation.
    // ---------------------------------------------------------------------

    bool confirmed_expectation_matched = false;

    error.clear();

    expect(
        state_store.erase_if_current(
            initial.fingerprint,
            "storage-01",
            confirmed_revision,
            &confirmed_expectation_matched,
            &error
        ) &&
        confirmed_expectation_matched,
        "successful revoke clears matching confirmed state: " +
            error
    );

    expect(
        !state_store.get(
            initial.fingerprint
        ).has_value(),
        "confirmed Storage authorization is absent after revoke"
    );


    // ---------------------------------------------------------------------
    // RT-009 deletion guard:
    //
    // Even with confirmed state absent, a durable pending transition proves a
    // Storage Node may already have accepted authorization. Physical deletion
    // remains blocked until the pending journal is durably cleared.
    // ---------------------------------------------------------------------

    pqnas::cluster::
        StorageUserProjectionTransitionRecord
            pending_transition;

    if (unplaced.has_value()) {
        pending_transition.fingerprint =
            initial.fingerprint;

        pending_transition.home_node =
            "storage-02";

        pending_transition.revision =
            unplaced->storage_projection_revision;
    }

    error.clear();

    expect(
        transition_store.mark_pending(
            pending_transition,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "pending Storage authorization persists: " +
            error
    );

    {
        bool erased = true;
        bool placement_blocked = true;
        bool cluster_revoke_pending = false;

        error.clear();

        const bool ok =
            pqnas::cluster::
                erase_user_persisted_cluster_safe(
                    users,
                    state_store,
                    transition_store,
                    transition_mutex,
                    users_path.string(),
                    initial.fingerprint,
                    &erased,
                    &placement_blocked,
                    &cluster_revoke_pending,
                    &error
                );

        expect(
            ok &&
            !erased &&
            !placement_blocked &&
            cluster_revoke_pending &&
            users.exists(
                initial.fingerprint
            ),
            "pending Storage authorization blocks physical deletion"
        );
    }

    bool pending_expectation_matched = false;

    error.clear();

    expect(
        transition_store.erase_if_current(
            initial.fingerprint,
            pending_transition.home_node,
            pending_transition.revision,
            &pending_expectation_matched,
            &error
        ) &&
        pending_expectation_matched,
        "successful pending recovery clears transition journal: " +
            error
    );

    // ---------------------------------------------------------------------
    // RT-004 case 3:
    //
    // User is unplaced and no confirmed Storage authorization remains.
    // Physical deletion is now safe and must persist to users.json.
    // ---------------------------------------------------------------------

    {
        bool erased = false;
        bool placement_blocked = true;
        bool cluster_revoke_pending = true;

        error.clear();

        const bool ok =
            pqnas::cluster::
                erase_user_persisted_cluster_safe(
                    users,
                    state_store,
                    transition_store,
                    transition_mutex,
                    users_path.string(),
                    initial.fingerprint,
                    &erased,
                    &placement_blocked,
                    &cluster_revoke_pending,
                    &error
                );

        expect(
            ok &&
            erased &&
            !placement_blocked &&
            !cluster_revoke_pending &&
            !users.exists(
                initial.fingerprint
            ),
            "user deletes only after confirmed Storage revoke"
        );
    }


    // Prove deletion reached the authoritative file, not merely memory.
    {
        pqnas::UsersRegistry reloaded;

        expect(
            reloaded.load(
                users_path.string()
            ),
            "users.json reloads after physical deletion"
        );

        expect(
            !reloaded.exists(
                initial.fingerprint
            ),
            "physical deletion survives users.json reload"
        );

        expect(
            reloaded.exists(
                sentinel.fingerprint
            ),
            "physical deletion preserves unrelated users"
        );
    }


    cleanup();


    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " RT-004 cluster-safe delete test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: RT-004 Director cluster-safe user-delete tests passed\n";

    return 0;
}
