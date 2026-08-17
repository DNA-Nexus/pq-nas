#include "users_registry.h"

#include <filesystem>
#include <fstream>
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
            "pqnas-user-delete-XXXXXX"
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

pqnas::UserRec user(
    char fingerprint_character,
    const std::string& home_node
) {
    pqnas::UserRec value;

    value.fingerprint =
        std::string(
            128,
            fingerprint_character
        );

    value.name = "delete test";
    value.role = "user";
    value.status = "disabled";
    value.storage_state = "unallocated";
    value.home_node = home_node;

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

    const fs::path users_path =
        directory / "users.json";

    pqnas::UsersRegistry users;
    bool projection_changed = false;
    std::string error;

    const auto unplaced =
        user(
            'a',
            ""
        );

    const auto placed =
        user(
            'b',
            "storage-01"
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

    expect(
        users.upsert_persisted(
            placed,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "placed user persisted"
    );

    // Cluster safety: a placed user cannot disappear from Director before
    // Storage revoke reconciliation exists.
    {
        bool erased = true;
        bool blocked = false;

        expect(
            users.erase_persisted_if_unplaced(
                placed.fingerprint,
                users_path.string(),
                &erased,
                &blocked,
                &error
            ) &&
            !erased &&
            blocked &&
            users.exists(
                placed.fingerprint
            ),
            "placed user deletion is blocked without mutation"
        );
    }

    // Ordinary standalone/unplaced deletion persists successfully.
    {
        bool erased = false;
        bool blocked = true;

        expect(
            users.erase_persisted_if_unplaced(
                unplaced.fingerprint,
                users_path.string(),
                &erased,
                &blocked,
                &error
            ) &&
            erased &&
            !blocked &&
            !users.exists(
                unplaced.fingerprint
            ),
            "unplaced user is deleted"
        );

        pqnas::UsersRegistry reloaded;

        expect(
            reloaded.load(
                users_path.string()
            ) &&
            !reloaded.exists(
                unplaced.fingerprint
            ) &&
            reloaded.exists(
                placed.fingerprint
            ),
            "successful deletion survives reload"
        );
    }

    // Add another unplaced user so a failed persistence attempt can prove that
    // the complete in-memory record is restored.
    const auto rollback_user =
        user(
            'c',
            ""
        );

    expect(
        users.upsert_persisted(
            rollback_user,
            users_path.string(),
            &projection_changed,
            &error
        ),
        "rollback test user persisted"
    );

    {
        const fs::path bad_parent =
            directory / "not_a_directory";

        {
            std::ofstream output(
                bad_parent
            );

            output << "file";
        }

        bool erased = true;
        bool blocked = true;

        expect(
            !users.erase_persisted_if_unplaced(
                rollback_user.fingerprint,
                (
                    bad_parent /
                    "users.json"
                ).string(),
                &erased,
                &blocked,
                &error
            ) &&
            !erased &&
            !blocked &&
            users.exists(
                rollback_user.fingerprint
            ),
            "persistence failure restores deleted in-memory user"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " persisted user-delete test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: persisted cluster-safe user-delete tests passed\n";

    return 0;
}
