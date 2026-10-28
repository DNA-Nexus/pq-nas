#include "cluster/storage_user_projection_sync_state_store.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
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
            "pqnas-projection-sync-state-XXXXXX"
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

pqnas::cluster::
StorageUserProjectionSyncState state(
    char fingerprint_character,
    const std::string& home_node,
    std::uint64_t revision
) {
    pqnas::cluster::
        StorageUserProjectionSyncState value;

    value.fingerprint =
        std::string(
            128,
            fingerprint_character
        );

    value.home_node =
        home_node;

    value.revision =
        revision;

    return value;
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

    const fs::path path =
        directory /
        "storage_projection_sync_state.json";

    pqnas::cluster::
        StorageUserProjectionSyncStateStore
            store;

    std::string error;

    expect(
        store.load(
            path.string(),
            &error
        ),
        "missing sync-state file loads as empty: " +
            error
    );

    expect(
        store.snapshot().empty(),
        "missing sync-state file produces empty snapshot"
    );

    const auto first =
        state(
            'a',
            "storage-01",
            17
        );

    expect(
        store.mark_synced(
            first,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "first confirmed synchronization is applied"
    );

    {
        const auto loaded =
            store.get(
                first.fingerprint
            );

        expect(
            loaded.has_value() &&
            loaded->home_node ==
                "storage-01" &&
            loaded->revision == 17,
            "confirmed synchronization is readable"
        );
    }

    expect(
        store.mark_synced(
            first,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Unchanged,
        "identical synchronization is idempotent"
    );

    expect(
        store.mark_synced(
            state(
                'a',
                "storage-01",
                16
            ),
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Stale,
        "older confirmed revision is rejected"
    );

    expect(
        store.mark_synced(
            state(
                'a',
                "storage-02",
                17
            ),
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Conflict,
        "same revision cannot point to a different Node"
    );

    expect(
        store.mark_synced(
            state(
                'a',
                "storage-02",
                18
            ),
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "newer revision may advance to a new Node"
    );

    {
        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                reloaded;

        expect(
            reloaded.load(
                path.string(),
                &error
            ),
            "persisted sync-state reloads: " +
                error
        );

        const auto loaded =
            reloaded.get(
                first.fingerprint
            );

        expect(
            loaded.has_value() &&
            loaded->home_node ==
                "storage-02" &&
            loaded->revision == 18,
            "new Node and revision survive reload"
        );
    }

    {
        struct stat stats {};

        expect(
            ::stat(
                path.c_str(),
                &stats
            ) == 0 &&
            S_ISREG(stats.st_mode) &&
            (stats.st_mode & 0077) == 0,
            "sync-state file is private"
        );
    }

    expect(
        store.mark_synced(
            state(
                'A',
                "storage-01",
                1
            ),
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    InvalidInput,
        "uppercase fingerprint fails closed"
    );

    expect(
        store.mark_synced(
            state(
                'b',
                "../bad-node",
                1
            ),
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    InvalidInput,
        "invalid Node ID fails closed"
    );

    expect(
        store.mark_synced(
            state(
                'b',
                "storage-01",
                0
            ),
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    InvalidInput,
        "revision zero fails closed"
    );

    // A fully durable snapshot is an idempotent ensure_durable no-op.
    {
        error.clear();

        expect(
            store.ensure_durable(
                &error
            ),
            "durable sync-state snapshot remains durable: " +
                error
        );

        const auto after_durability_check =
            store.get(
                first.fingerprint
            );

        expect(
            after_durability_check.has_value() &&
            after_durability_check->home_node ==
                "storage-02" &&
            after_durability_check->revision == 18,
            "ensure_durable must preserve the current confirmed snapshot"
        );
    }


    // Stale erase expectation must not remove newer knowledge.
    {
        bool matched = true;

        expect(
            store.erase_if_current(
                first.fingerprint,
                "storage-01",
                17,
                &matched,
                &error
            ) &&
            !matched,
            "stale erase expectation is a safe no-op"
        );

        const auto still_present =
            store.get(
                first.fingerprint
            );

        expect(
            still_present.has_value() &&
            still_present->revision == 18,
            "stale erase preserves current state"
        );
    }

    // Matching delete is used after successful revoke of a deleted user.
    {
        bool matched = false;

        expect(
            store.erase_if_current(
                first.fingerprint,
                "storage-02",
                18,
                &matched,
                &error
            ) &&
            matched &&
            !store.get(
                first.fingerprint
            ).has_value(),
            "matching erase removes confirmed state"
        );
    }

    // Re-add one state so failed load can prove fail-closed snapshot behavior.
    const auto preserved =
        state(
            'c',
            "storage-03",
            9
        );

    expect(
        store.mark_synced(
            preserved,
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "state restored for malformed-load test"
    );

    {
        std::ofstream output(
            path,
            std::ios::trunc
        );

        output
            << "{\"version\":1,\"states\":["
               "{\"fingerprint\":\"broken\"}]}";

        output.close();

        expect(
            !store.load(
                path.string(),
                &error
            ),
            "malformed persisted state fails closed"
        );

        const auto current =
            store.get(
                preserved.fingerprint
            );

        expect(
            current.has_value() &&
            current->home_node ==
                preserved.home_node &&
            current->revision ==
                preserved.revision,
            "failed load does not replace in-memory snapshot"
        );
    }

    // Restore a valid file before symlink test.
    const fs::path valid_path =
        directory /
        "valid_state.json";

    pqnas::cluster::
        StorageUserProjectionSyncStateStore
            valid_store;

    expect(
        valid_store.load(
            valid_path.string(),
            &error
        ),
        "second state store loads"
    );

    expect(
        valid_store.mark_synced(
            state(
                'd',
                "storage-04",
                3
            ),
            &error
        ) ==
            pqnas::cluster::
                StorageUserProjectionSyncStateStoreResult::
                    Applied,
        "second state store persists"
    );

    const fs::path symlink_path =
        directory /
        "symlink_state.json";

    std::error_code symlink_error;

    fs::create_symlink(
        valid_path.filename(),
        symlink_path,
        symlink_error
    );

    expect(
        !symlink_error,
        "sync-state symlink test fixture created"
    );

    if (!symlink_error) {
        pqnas::cluster::
            StorageUserProjectionSyncStateStore
                symlink_store;

        expect(
            !symlink_store.load(
                symlink_path.string(),
                &error
            ),
            "symlinked sync-state target is rejected"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection sync-state store test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection sync-state store tests passed\n";

    return 0;
}
