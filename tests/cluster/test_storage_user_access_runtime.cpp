#include "cluster/storage_user_access_runtime.h"
#include "cluster/storage_user_session_cookie.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
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
            "pqnas-storage-access-runtime-XXXXXX"
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

pqnas::cluster::StorageUserProjection
make_projection(
    const std::string& fingerprint,
    std::uint64_t revision,
    const std::string& pool_id
) {
    pqnas::cluster::StorageUserProjection
        projection;

    projection.fingerprint =
        fingerprint;

    projection.enabled = true;
    projection.storage_allocated = true;

    projection.quota_bytes =
        1024ull * 1024ull;

    projection.root_rel =
        "users/example";

    projection.storage_pool_id =
        pool_id;

    projection.home_node =
        "storage-01";

    projection.revision =
        revision;

    return projection;
}

} // namespace

int main() {
    using namespace pqnas::cluster;

    const fs::path temp_root =
        make_test_directory();

    if (temp_root.empty()) {
        std::cerr
            << "FAIL: could not create temporary directory\n";

        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code ec;

        fs::remove_all(
            temp_root,
            ec
        );
    };

    const fs::path projection_path =
        temp_root /
        "storage_user_projections.json";

    const fs::path pool_data_root =
        temp_root /
        "pool_01_data";

    std::error_code ec;

    fs::create_directories(
        pool_data_root /
        "users" /
        "example",
        ec
    );

    expect(
        !ec,
        "create pool data root"
    );

    StorageUserProjectionStore
        projection_store;

    std::string error;

    expect(
        projection_store.load(
            projection_path.string(),
            &error
        ),
        "load empty projection store: " +
            error
    );

    const std::string fingerprint(
        128,
        'a'
    );

    auto projection =
        make_projection(
            fingerprint,
            17,
            "pool_01"
        );

    expect(
        projection_store.apply(
            projection,
            &error
        ) ==
            StorageUserProjectionStoreResult::
                Applied,
        "apply initial projection: " +
            error
    );

    std::array<unsigned char, 32>
        base_cookie_key{};

    for (std::size_t i = 0;
         i < base_cookie_key.size();
         ++i) {
        base_cookie_key[i] =
            static_cast<unsigned char>(
                i + 1
            );
    }

    long now =
        1200;

    StorageUserAccessRuntimeConfig
        runtime_config;

    runtime_config.projections =
        &projection_store;

    runtime_config.base_cookie_key =
        base_cookie_key.data();

    runtime_config.local_node_id =
        "storage-01";

    runtime_config.clock =
        [&now]() -> long {
            return now;
        };

    runtime_config.resolve_pool_data_root =
        [&](const std::string& pool_id,
            fs::path* data_root) -> bool {
            if (data_root) {
                data_root->clear();
            }

            if (!data_root ||
                pool_id != "pool_01") {
                return false;
            }

            *data_root =
                pool_data_root;

            return true;
        };

    StorageUserAccessRuntime
        runtime(
            runtime_config
        );

    std::string cookie;

    expect(
        storage_user_session_cookie_mint(
            base_cookie_key.data(),
            "storage-01",
            fingerprint,
            17,
            1000,
            1600,
            &cookie
        ),
        "mint revision-17 Storage cookie"
    );

    StorageUserSessionAuthenticatorOutput
        auth_output;

    expect(
        runtime
            .request_auth_context()
            .authenticate(
                cookie,
                &auth_output
            ) ==
            StorageUserSessionAuthenticatorResult::
                Authenticated,
        "runtime authenticates matching Storage session"
    );

    expect(
        auth_output.fingerprint ==
                fingerprint &&
            auth_output.role ==
                "user" &&
            auth_output.projection_revision ==
                17,
        "runtime authentication output is projection-bound user identity"
    );

    StorageUserRootResolveOutput
        root_output;

    expect(
        runtime
            .root_resolver()
            .resolve(
                fingerprint,
                &root_output
            ) ==
            StorageUserRootResolveResult::
                Resolved,
        "runtime resolves authorized user root"
    );

    expect(
        root_output.user_root ==
            (
                pool_data_root /
                "users" /
                "example"
            ).lexically_normal() &&
            root_output.projection_revision ==
                17 &&
            root_output.storage_pool_id ==
                "pool_01",
        "runtime root comes from projection and strict pool mapping"
    );

    // Security: changing the authoritative projection revision must invalidate
    // the already-issued Storage session on its next authenticated request.
    projection.revision =
        18;

    expect(
        projection_store.apply(
            projection,
            &error
        ) ==
            StorageUserProjectionStoreResult::
                Applied,
        "apply newer projection revision"
    );

    auth_output = {};

    expect(
        runtime
            .request_auth_context()
            .authenticate(
                cookie,
                &auth_output
            ) ==
            StorageUserSessionAuthenticatorResult::
                Rejected,
        "stale Storage session rejected after projection revision change"
    );

    root_output = {};

    expect(
        runtime
            .root_resolver()
            .resolve(
                fingerprint,
                &root_output
            ) ==
            StorageUserRootResolveResult::
                Resolved &&
            root_output.projection_revision ==
                18,
        "root resolver observes fresh projection revision"
    );

    // Security: unknown named pools must fail closed. The runtime must never
    // substitute the default pool for an unavailable named pool.
    projection.revision =
        19;

    projection.storage_pool_id =
        "pool_02";

    expect(
        projection_store.apply(
            projection,
            &error
        ) ==
            StorageUserProjectionStoreResult::
                Applied,
        "apply unknown-pool projection"
    );

    root_output = {};

    expect(
        runtime
            .root_resolver()
            .resolve(
                fingerprint,
                &root_output
            ) ==
            StorageUserRootResolveResult::
                PoolUnavailable,
        "unknown named pool fails closed"
    );

    expect(
        root_output.user_root.empty(),
        "failed root resolution leaves no stale filesystem root"
    );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " Storage user access runtime test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage user access runtime tests passed\n";

    return 0;
}
