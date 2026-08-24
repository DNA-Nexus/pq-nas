#include "cluster/storage_projection_startup_consistency.h"

#include "cluster/storage_user_projection_sync_state_store.h"
#include "cluster/storage_user_projection_transition_store.h"
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
            "pqnas-startup-consistency-XXXXXX"
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
        std::string(
            128,
            'a'
        );

    user.name =
        "startup consistency";

    user.role =
        "user";

    user.status =
        "enabled";

    user.home_node =
        "storage-01";

    user.storage_state =
        "unallocated";

    user.quota_bytes = 0;

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

    const fs::path state_path =
        directory /
        "storage_projection_sync_state.json";

    const fs::path transition_path =
        directory /
        "storage_projection_transition_journal.json";

    pqnas::UsersRegistry users;

    pqnas::cluster::
        StorageUserProjectionSyncStateStore
            state_store;

    pqnas::cluster::
        StorageUserProjectionTransitionStore
            transition_store;

    std::string error;

    bool projection_changed = false;

    const pqnas::UserRec user =
        make_user();

    expect(
        users.upsert_persisted(
            user,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "authoritative user persists"
    );

    expect(
        projection_changed,
        "new user establishes projection revision"
    );

    expect(
        state_store.load(
            state_path.string(),
            &error
        ),
        "confirmed-state store loads"
    );

    error.clear();

    expect(
        transition_store.load(
            transition_path.string(),
            &error
        ),
        "pending transition store loads"
    );

    const auto current =
        users.get(
            user.fingerprint
        );

    expect(
        current.has_value(),
        "persisted user remains readable"
    );

    if (!current.has_value()) {
        cleanup();
        return 1;
    }

    // No confirmed state cannot be ahead of users.json.
    {
        const auto result =
            pqnas::cluster::
                check_storage_projection_startup_consistency(
                    users,
                    state_store,
                    transition_store
                );

        expect(
            result.ok &&
            result.confirmed_states_checked == 0 &&
            result.pending_states_checked == 0 &&
            result.confirmed_revision_ahead == 0 &&
            result.pending_revision_ahead == 0,
            "empty durable projection state is startup-consistent"
        );
    }

    pqnas::cluster::
        StorageUserProjectionSyncState
            confirmed;

    confirmed.fingerprint =
        user.fingerprint;

    confirmed.home_node =
        "storage-01";

    confirmed.revision =
        current->storage_projection_revision;

    expect(
        state_store.mark_synced(
            confirmed,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "equal confirmed revision persists"
    );

    // Equal revision is not the RT-003 rollback condition.
    {
        const auto result =
            pqnas::cluster::
                check_storage_projection_startup_consistency(
                    users,
                    state_store,
                    transition_store
                );

        expect(
            result.ok &&
            result.confirmed_states_checked == 1 &&
            result.pending_states_checked == 0 &&
            result.confirmed_revision_ahead == 0 &&
            result.pending_revision_ahead == 0,
            "equal confirmed revision is startup-consistent"
        );
    }

    pqnas::cluster::
        StorageUserProjectionTransitionRecord
            pending;

    pending.fingerprint =
        user.fingerprint;

    pending.home_node =
        "storage-02";

    pending.revision =
        current->storage_projection_revision;

    error.clear();

    expect(
        transition_store.mark_pending(
            pending,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "equal pending revision persists"
    );

    {
        const auto result =
            pqnas::cluster::
                check_storage_projection_startup_consistency(
                    users,
                    state_store,
                    transition_store
                );

        expect(
            result.ok &&
            result.confirmed_states_checked == 1 &&
            result.pending_states_checked == 1 &&
            result.confirmed_revision_ahead == 0 &&
            result.pending_revision_ahead == 0,
            "equal pending revision is startup-consistent"
        );
    }

    ++pending.revision;

    error.clear();

    expect(
        transition_store.mark_pending(
            pending,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "newer pending revision fixture persists"
    );

    {
        const auto result =
            pqnas::cluster::
                check_storage_projection_startup_consistency(
                    users,
                    state_store,
                    transition_store
                );

        expect(
            !result.ok &&
            result.confirmed_revision_ahead == 0 &&
            result.pending_states_checked == 1 &&
            result.pending_revision_ahead == 1,
            "pending revision newer than users.json fails closed"
        );
    }

    bool pending_expectation_matched = false;

    error.clear();

    expect(
        transition_store.erase_if_current(
            pending.fingerprint,
            pending.home_node,
            pending.revision,
            &pending_expectation_matched,
            &error
        ) &&
        pending_expectation_matched,
        "pending rollback fixture clears"
    );

    ++confirmed.revision;

    expect(
        state_store.mark_synced(
            confirmed,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "newer confirmed revision fixture persists"
    );

    // Simulates users.json rollback after the derived confirmed state was
    // already persisted durably.
    {
        const auto result =
            pqnas::cluster::
                check_storage_projection_startup_consistency(
                    users,
                    state_store,
                    transition_store
                );

        expect(
            !result.ok &&
            result.confirmed_states_checked == 1 &&
            result.pending_states_checked == 0 &&
            result.confirmed_revision_ahead == 1 &&
            result.pending_revision_ahead == 0,
            "confirmed revision newer than users.json fails closed"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " startup consistency test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage projection startup consistency tests passed\n";

    return 0;
}
