#include "cluster/storage_user_root_resolver.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

using pqnas::cluster::
    StorageUserProjection;

using pqnas::cluster::
    StorageUserProjectionStore;

using pqnas::cluster::
    StorageUserProjectionStoreResult;

using pqnas::cluster::
    StorageUserRootResolveOutput;

using pqnas::cluster::
    StorageUserRootResolveResult;

using pqnas::cluster::
    StorageUserRootResolver;

using pqnas::cluster::
    StorageUserRootResolverConfig;

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
    StorageUserRootResolveResult actual,
    StorageUserRootResolveResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                storage_user_root_resolve_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                storage_user_root_resolve_result_code(
                    actual
                )
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-storage-root-resolver-test-XXXXXX"
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

StorageUserProjection active_projection(
    const std::string& fingerprint,
    std::uint64_t revision,
    const std::string& pool_id = "pool_01"
) {
    StorageUserProjection projection;

    projection.fingerprint =
        fingerprint;

    projection.enabled = true;

    projection.storage_allocated =
        true;

    projection.quota_bytes =
        10ull * 1024ull * 1024ull;

    projection.root_rel =
        "users/" + fingerprint;

    projection.storage_pool_id =
        pool_id;

    projection.home_node =
        "storage-01";

    projection.revision =
        revision;

    return projection;
}

void expect_output_empty(
    const StorageUserRootResolveOutput& output,
    const std::string& message
) {
    expect(
        output.user_root.empty() &&
        output.projection_revision == 0 &&
        output.quota_bytes == 0 &&
        output.storage_pool_id.empty() &&
        output.root_rel.empty(),
        message
    );
}

bool apply_projection(
    StorageUserProjectionStore* store,
    const StorageUserProjection& projection,
    const std::string& message
) {
    if (!store) {
        expect(false, message + ": null store");
        return false;
    }

    std::string error;

    const auto result =
        store->apply(
            projection,
            &error
        );

    expect(
        result ==
            StorageUserProjectionStoreResult::
                Applied,
        message +
            ": " +
            error
    );

    return
        result ==
        StorageUserProjectionStoreResult::
            Applied;
}

} // namespace

int main() {
    const std::string fingerprint(
        128,
        'a'
    );

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

    std::string store_error;

    if (!store.load(
            store_path.string(),
            &store_error)) {
        std::cerr
            << "FAIL: projection store load failed: "
            << store_error
            << '\n';

        cleanup();
        return 1;
    }

    int pool_lookup_calls = 0;

    StorageUserRootResolverConfig config;

    config.projections =
        &store;

    config.local_node_id =
        "storage-01";

    config.resolve_pool_data_root =
        [&](const std::string& pool_id,
            fs::path* data_root) -> bool {
            ++pool_lookup_calls;

            if (!data_root) {
                return false;
            }

            data_root->clear();

            if (pool_id.empty()) {
                *data_root =
                    root /
                    "default-data";

                return true;
            }

            if (pool_id == "pool_01") {
                *data_root =
                    root /
                    "pool-01" /
                    "data";

                return true;
            }

            // Security regression: an unknown named pool must remain
            // unavailable. Never substitute the default pool.
            return false;
        };

    StorageUserRootResolver resolver(
        std::move(config)
    );

    StorageUserRootResolveOutput output;

    // Invalid identity must fail before any projection/pool resolution.
    expect_result(
        resolver.resolve(
            std::string(127, 'a'),
            &output
        ),
        StorageUserRootResolveResult::
            InvalidInput,
        "malformed fingerprint fails closed"
    );

    expect(
        pool_lookup_calls == 0,
        "invalid fingerprint performs no pool lookup"
    );

    expect_output_empty(
        output,
        "invalid fingerprint leaves empty output"
    );

    // Missing projection must not invent a legacy/default user root.
    expect_result(
        resolver.resolve(
            fingerprint,
            &output
        ),
        StorageUserRootResolveResult::
            ProjectionMissing,
        "missing projection fails closed"
    );

    expect(
        pool_lookup_calls == 0,
        "missing projection performs no pool lookup"
    );

    expect_output_empty(
        output,
        "missing projection leaves empty output"
    );

    // Default pool is represented by the empty logical pool id.
    auto projection =
        active_projection(
            fingerprint,
            10,
            ""
        );

    if (apply_projection(
            &store,
            projection,
            "default-pool projection applied")) {

        expect_result(
            resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                Resolved,
            "default pool resolves"
        );

        const fs::path expected =
            (
                root /
                "default-data" /
                projection.root_rel
            ).lexically_normal();

        expect(
            output.user_root == expected &&
            output.projection_revision == 10 &&
            output.quota_bytes ==
                projection.quota_bytes &&
            output.storage_pool_id.empty() &&
            output.root_rel ==
                projection.root_rel,
            "default pool returns projection-bound user root"
        );
    }

    // Explicit named pool resolves only through the local pool callback.
    projection =
        active_projection(
            fingerprint,
            11,
            "pool_01"
        );

    if (apply_projection(
            &store,
            projection,
            "named-pool projection applied")) {

        expect_result(
            resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                Resolved,
            "named pool resolves"
        );

        const fs::path expected =
            (
                root /
                "pool-01" /
                "data" /
                projection.root_rel
            ).lexically_normal();

        expect(
            output.user_root == expected &&
            output.projection_revision == 11 &&
            output.storage_pool_id ==
                "pool_01",
            "named pool returns correct local user root"
        );
    }

    // Security regression: a projection naming an unavailable pool must
    // never fall back to the default pool or preserve the previous root.
    projection =
        active_projection(
            fingerprint,
            12,
            "pool_missing"
        );

    if (apply_projection(
            &store,
            projection,
            "missing-pool projection applied")) {

        expect_result(
            resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                PoolUnavailable,
            "unknown named pool fails closed"
        );

        expect_output_empty(
            output,
            "unknown named pool clears previous resolved root"
        );
    }

    projection =
        active_projection(
            fingerprint,
            13,
            "pool_01"
        );

    projection.enabled = false;

    if (apply_projection(
            &store,
            projection,
            "disabled projection applied")) {

        const int before =
            pool_lookup_calls;

        expect_result(
            resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                Unauthorized,
            "disabled projection fails closed"
        );

        expect(
            pool_lookup_calls == before,
            "disabled projection performs no pool lookup"
        );

        expect_output_empty(
            output,
            "disabled projection leaves empty output"
        );
    }

    // Valid deallocation tombstones deliberately carry no physical mapping.
    projection =
        active_projection(
            fingerprint,
            14,
            "pool_01"
        );

    projection.storage_allocated = false;
    projection.quota_bytes = 0;
    projection.root_rel.clear();
    projection.storage_pool_id.clear();

    if (apply_projection(
            &store,
            projection,
            "unallocated tombstone applied")) {

        expect_result(
            resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                Unauthorized,
            "unallocated projection fails closed"
        );

        expect_output_empty(
            output,
            "unallocated projection leaves empty output"
        );
    }

    projection =
        active_projection(
            fingerprint,
            15,
            "pool_01"
        );

    projection.home_node =
        "storage-02";

    if (apply_projection(
            &store,
            projection,
            "foreign-node projection applied")) {

        expect_result(
            resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                Unauthorized,
            "foreign home node fails closed"
        );

        expect_output_empty(
            output,
            "foreign home node leaves empty output"
        );
    }

    // Restore an authorized local projection before infrastructure tests.
    projection =
        active_projection(
            fingerprint,
            16,
            "pool_01"
        );

    apply_projection(
        &store,
        projection,
        "local projection restored"
    );

    {
        StorageUserRootResolverConfig bad;

        bad.projections =
            &store;

        bad.local_node_id =
            "storage-01";

        StorageUserRootResolver
            invalid_config_resolver(
                std::move(bad)
            );

        output.user_root =
            "/stale/root";

        output.projection_revision =
            999;

        expect_result(
            invalid_config_resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                InvalidConfiguration,
            "missing pool callback fails configuration"
        );

        expect_output_empty(
            output,
            "invalid configuration clears stale output"
        );
    }

    {
        StorageUserRootResolverConfig throwing;

        throwing.projections =
            &store;

        throwing.local_node_id =
            "storage-01";

        throwing.resolve_pool_data_root =
            [](const std::string&,
               fs::path*) -> bool {
                throw 1;
            };

        StorageUserRootResolver
            throwing_resolver(
                std::move(throwing)
            );

        expect_result(
            throwing_resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                InternalError,
            "throwing pool resolver fails closed"
        );

        expect_output_empty(
            output,
            "throwing pool resolver leaves empty output"
        );
    }

    {
        StorageUserRootResolverConfig relative;

        relative.projections =
            &store;

        relative.local_node_id =
            "storage-01";

        relative.resolve_pool_data_root =
            [](const std::string&,
               fs::path* data_root) -> bool {
                if (!data_root) {
                    return false;
                }

                *data_root =
                    "relative/data";

                return true;
            };

        StorageUserRootResolver
            relative_root_resolver(
                std::move(relative)
            );

        expect_result(
            relative_root_resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                PoolUnavailable,
            "relative pool data root fails closed"
        );

        expect_output_empty(
            output,
            "relative pool data root leaves empty output"
        );
    }

    // First prove the current rev16 projection still resolves.
    expect_result(
        resolver.resolve(
            fingerprint,
            &output
        ),
        StorageUserRootResolveResult::
            Resolved,
        "rev16 projection resolves before revoke"
    );

    expect(
        output.projection_revision == 16 &&
        !output.user_root.empty(),
        "rev16 resolution returns current root"
    );

    // Matching authenticated revision may use this freshly resolved root.
    expect_result(
        resolver.resolve_at_revision(
            fingerprint,
            16,
            &output
        ),
        StorageUserRootResolveResult::
            Resolved,
        "matching authenticated revision resolves root"
    );

    expect(
        output.projection_revision == 16 &&
        !output.user_root.empty(),
        "matching revision preserves revision-bound root"
    );

    // Security regression: a request authenticated under an older projection
    // must not cross into a root resolved from the current newer projection.
    expect_result(
        resolver.resolve_at_revision(
            fingerprint,
            15,
            &output
        ),
        StorageUserRootResolveResult::
            Unauthorized,
        "mismatched authenticated revision fails closed"
    );

    expect_output_empty(
        output,
        "revision mismatch clears resolved filesystem root"
    );

    expect_result(
        resolver.resolve_at_revision(
            fingerprint,
            0,
            &output
        ),
        StorageUserRootResolveResult::
            InvalidInput,
        "zero authenticated revision is rejected"
    );

    expect_output_empty(
        output,
        "zero authenticated revision leaves empty output"
    );

    // Security regression: resolver must fetch the projection again on every
    // call. A rev17 tombstone must invalidate the previously resolved root.
    StorageUserProjection tombstone =
        active_projection(
            fingerprint,
            17,
            "pool_01"
        );

    tombstone.enabled = false;
    tombstone.storage_allocated = false;
    tombstone.quota_bytes = 0;
    tombstone.root_rel.clear();
    tombstone.storage_pool_id.clear();

    if (apply_projection(
            &store,
            tombstone,
            "rev17 revoke tombstone applied")) {

        expect_result(
            resolver.resolve(
                fingerprint,
                &output
            ),
            StorageUserRootResolveResult::
                Unauthorized,
            "rev17 tombstone revokes prior resolved root"
        );

        expect_output_empty(
            output,
            "revocation clears prior positive root output"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " Storage user root resolver test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage user root resolver tests passed\n";

    return 0;
}
