#include "cluster/storage_user_projection_store.h"

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

void expect_result(
    pqnas::cluster::StorageUserProjectionStoreResult actual,
    pqnas::cluster::StorageUserProjectionStoreResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                storage_user_projection_store_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                storage_user_projection_store_result_code(
                    actual
                )
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-storage-projection-test-XXXXXX"
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

bool write_text(
    const fs::path& path,
    const std::string& contents
) {
    std::ofstream output(
        path,
        std::ios::binary |
        std::ios::trunc
    );

    if (!output.good()) {
        return false;
    }

    output << contents;
    output.flush();

    return output.good();
}

pqnas::cluster::StorageUserProjection
allocated_projection(
    char fingerprint_char,
    std::uint64_t revision
) {
    pqnas::cluster::StorageUserProjection projection;

    projection.fingerprint =
        std::string(
            128,
            fingerprint_char
        );

    projection.enabled = true;
    projection.storage_allocated = true;
    projection.quota_bytes =
        10ull * 1024ull * 1024ull * 1024ull;

    projection.root_rel =
        "users/" +
        projection.fingerprint;

    projection.storage_pool_id =
        "pool_01";

    projection.home_node =
        "storage-01";

    projection.revision =
        revision;

    return projection;
}

} // namespace

int main() {
    using pqnas::cluster::
        StorageUserProjectionStore;
    using pqnas::cluster::
        StorageUserProjectionStoreResult;

    const fs::path root =
        make_test_directory();

    if (root.empty()) {
        std::cerr
            << "FAIL: could not create test directory\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(
            root,
            error
        );
    };

    const fs::path store_path =
        root /
        "storage_user_projections.json";

    StorageUserProjectionStore store;
    std::string error;

    expect(
        store.load(
            store_path.string(),
            &error
        ),
        "missing projection store loads as empty: " +
            error
    );

    expect(
        store.snapshot().empty(),
        "missing projection store produces empty state"
    );

    auto first =
        allocated_projection(
            'a',
            10
        );

    expect_result(
        store.apply(
            first,
            &error
        ),
        StorageUserProjectionStoreResult::Applied,
        "initial projection applied"
    );

    {
        const auto loaded =
            store.get(
                first.fingerprint
            );

        expect(
            loaded.has_value(),
            "applied projection can be read"
        );

        if (loaded) {
            expect(
                loaded->revision == 10 &&
                loaded->home_node ==
                    "storage-01" &&
                loaded->storage_allocated &&
                loaded->enabled,
                "applied projection fields preserved"
            );
        }
    }

    expect_result(
        store.apply(
            first,
            &error
        ),
        StorageUserProjectionStoreResult::Unchanged,
        "same revision and identical content is idempotent"
    );

    auto conflicting =
        first;

    conflicting.quota_bytes += 1;

    expect_result(
        store.apply(
            conflicting,
            &error
        ),
        StorageUserProjectionStoreResult::Conflict,
        "same revision with different content conflicts"
    );

    auto stale =
        first;

    stale.revision = 9;

    expect_result(
        store.apply(
            stale,
            &error
        ),
        StorageUserProjectionStoreResult::Stale,
        "older revision rejected"
    );

    auto newer =
        first;

    newer.revision = 11;
    newer.quota_bytes *= 2;

    expect_result(
        store.apply(
            newer,
            &error
        ),
        StorageUserProjectionStoreResult::Applied,
        "newer revision replaces prior projection"
    );

    {
        auto invalid =
            allocated_projection(
                'a',
                12
            );

        invalid.fingerprint[5] = 'A';

        expect_result(
            store.apply(
                invalid,
                &error
            ),
            StorageUserProjectionStoreResult::
                InvalidInput,
            "uppercase fingerprint rejected"
        );
    }

    {
        auto invalid =
            allocated_projection(
                'a',
                12
            );

        invalid.home_node =
            "../node";

        expect_result(
            store.apply(
                invalid,
                &error
            ),
            StorageUserProjectionStoreResult::
                InvalidInput,
            "invalid home node rejected"
        );
    }

    {
        auto invalid =
            allocated_projection(
                'a',
                12
            );

        invalid.storage_pool_id =
            "pool-01";

        expect_result(
            store.apply(
                invalid,
                &error
            ),
            StorageUserProjectionStoreResult::
                InvalidInput,
            "invalid storage pool rejected"
        );
    }

    {
        auto invalid =
            allocated_projection(
                'a',
                12
            );

        invalid.root_rel =
            "users/foo/../bar";

        expect_result(
            store.apply(
                invalid,
                &error
            ),
            StorageUserProjectionStoreResult::
                InvalidInput,
            "non-canonical control-plane root rejected"
        );
    }

    {
        auto invalid =
            allocated_projection(
                'a',
                12
            );

        invalid.root_rel =
            "users/.pqnas/private";

        expect_result(
            store.apply(
                invalid,
                &error
            ),
            StorageUserProjectionStoreResult::
                InvalidInput,
            "reserved storage namespace rejected"
        );
    }

    {
        auto invalid =
            allocated_projection(
                'a',
                0
            );

        expect_result(
            store.apply(
                invalid,
                &error
            ),
            StorageUserProjectionStoreResult::
                InvalidInput,
            "zero revision rejected"
        );
    }

    {
        auto invalid =
            allocated_projection(
                'b',
                1
            );

        invalid.storage_allocated = false;

        expect_result(
            store.apply(
                invalid,
                &error
            ),
            StorageUserProjectionStoreResult::
                InvalidInput,
            "unallocated projection cannot retain storage metadata"
        );
    }

    pqnas::cluster::StorageUserProjection
        unallocated;

    unallocated.fingerprint =
        std::string(128, 'b');
    unallocated.enabled = false;
    unallocated.storage_allocated = false;
    unallocated.quota_bytes = 0;
    unallocated.root_rel.clear();
    unallocated.storage_pool_id.clear();
    unallocated.home_node =
        "storage-01";
    unallocated.revision = 1;

    expect_result(
        store.apply(
            unallocated,
            &error
        ),
        StorageUserProjectionStoreResult::Applied,
        "canonical unallocated projection accepted"
    );

    {
        struct stat stats {};

        expect(
            ::stat(
                store_path.c_str(),
                &stats) == 0,
            "projection store exists after apply"
        );

        if (::stat(
                store_path.c_str(),
                &stats) == 0) {
            expect(
                (stats.st_mode & 0777) == 0600,
                "projection store is persisted with mode 0600"
            );
        }
    }

    StorageUserProjectionStore reloaded;

    error.clear();

    expect(
        reloaded.load(
            store_path.string(),
            &error
        ),
        "persisted projection store reloads: " +
            error
    );

    {
        const auto snapshot =
            reloaded.snapshot();

        expect(
            snapshot.size() == 2,
            "both projections survive reload"
        );

        if (snapshot.size() == 2) {
            expect(
                snapshot[0].fingerprint <
                    snapshot[1].fingerprint,
                "snapshot order is deterministic"
            );
        }
    }

    {
        const auto before =
            reloaded.get(
                first.fingerprint
            );

        expect(
            before.has_value(),
            "reload contains first projection before malformed-load test"
        );

        expect(
            write_text(
                store_path,
                "{not-json\n"
            ),
            "malformed store test file written"
        );

        expect(
            ::chmod(
                store_path.c_str(),
                0600) == 0,
            "malformed store test mode restored"
        );

        error.clear();

        expect(
            !reloaded.load(
                store_path.string(),
                &error
            ),
            "malformed persisted store fails closed"
        );

        const auto after =
            reloaded.get(
                first.fingerprint
            );

        expect(
            before.has_value() &&
            after.has_value() &&
            after->revision ==
                before->revision,
            "failed reload does not replace in-memory snapshot"
        );
    }

    {
        const fs::path target =
            root /
            "symlink-target.json";

        const fs::path link =
            root /
            "symlink-store.json";

        expect(
            write_text(
                target,
                "{}\n"
            ),
            "symlink target created"
        );

        expect(
            ::chmod(
                target.c_str(),
                0600) == 0,
            "symlink target secured"
        );

        expect(
            ::symlink(
                target.c_str(),
                link.c_str()) == 0,
            "projection-store symlink created"
        );

        StorageUserProjectionStore unsafe;
        error.clear();

        expect(
            !unsafe.load(
                link.string(),
                &error
            ),
            "symlinked projection store fails closed"
        );
    }

    {
        const fs::path readable =
            root /
            "group-readable.json";

        expect(
            write_text(
                readable,
                "{}\n"
            ),
            "group-readable test file created"
        );

        expect(
            ::chmod(
                readable.c_str(),
                0640) == 0,
            "group-readable test permissions installed"
        );

        StorageUserProjectionStore unsafe;
        error.clear();

        expect(
            !unsafe.load(
                readable.string(),
                &error
            ),
            "group-readable projection store fails closed"
        );
    }

    {
        const fs::path unsafe_directory =
            root /
            "unsafe-directory";

        std::error_code directory_error;

        fs::create_directory(
            unsafe_directory,
            directory_error
        );

        expect(
            !directory_error,
            "unsafe-directory test directory created"
        );

        expect(
            ::chmod(
                unsafe_directory.c_str(),
                0770) == 0,
            "unsafe-directory permissions installed"
        );

        StorageUserProjectionStore unsafe;
        error.clear();

        expect(
            !unsafe.load(
                (
                    unsafe_directory /
                    "state.json"
                ).string(),
                &error
            ),
            "group-writable projection directory fails closed"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " storage user projection store test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection store tests passed\n";

    return 0;
}
