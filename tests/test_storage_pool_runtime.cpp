#include "storage_pool_runtime.h"

#include <filesystem>
#include <iostream>
#include <string>

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

} // namespace

int main() {
    fs::path out;

    expect(
        !pqnas::storage_pool_runtime_reset(
            "relative/default"
        ),
        "relative default root rejected"
    );

    expect(
        pqnas::storage_pool_runtime_reset(
            "/srv/pqnas/data"
        ),
        "absolute default root accepted"
    );

    expect(
        pqnas::resolve_storage_pool_data_root_strict(
            "",
            &out
        ) &&
        out == fs::path("/srv/pqnas/data"),
        "empty pool id resolves default data root"
    );

    expect(
        pqnas::resolve_storage_pool_data_root_strict(
            "default",
            &out
        ) &&
        out == fs::path("/srv/pqnas/data"),
        "default compatibility alias resolves default data root"
    );

    expect(
        !pqnas::storage_pool_runtime_set_named_mount(
            "default",
            "/srv/pqnas/pools/default"
        ),
        "reserved default id cannot be registered"
    );

    expect(
        !pqnas::storage_pool_runtime_set_named_mount(
            "../evil",
            "/srv/pqnas/pools/evil"
        ),
        "invalid pool id rejected"
    );

    expect(
        !pqnas::storage_pool_runtime_set_named_mount(
            "pool_01",
            "relative/pool"
        ),
        "relative named mount rejected"
    );

    expect(
        !pqnas::storage_pool_runtime_set_named_mount(
            "pool_01",
            "/srv/pqnas/pools/../escape"
        ),
        "lexical traversal outside pools root rejected"
    );

    expect(
        !pqnas::storage_pool_runtime_set_named_mount(
            "pool_01",
            "/srv/pqnas/pools_evil/pool_01"
        ),
        "sibling prefix outside pools root rejected"
    );

    expect(
        !pqnas::storage_pool_runtime_set_named_mount(
            "pool_01",
            "/srv/pqnas/pools"
        ),
        "pools root itself rejected as named pool mount"
    );

    expect(
        pqnas::storage_pool_runtime_set_named_mount(
            "pool_01",
            "/srv/pqnas/pools/pool_01"
        ),
        "valid named pool registered"
    );

    expect(
        pqnas::resolve_storage_pool_data_root_strict(
            "pool_01",
            &out
        ) &&
        out ==
            fs::path(
                "/srv/pqnas/pools/pool_01/data"
            ),
        "named pool resolves its own data root"
    );

    out = "/stale/value";

    expect(
        !pqnas::resolve_storage_pool_data_root_strict(
            "pool_missing",
            &out
        ),
        "unknown named pool fails closed"
    );

    expect(
        out.empty(),
        "unknown named pool does not preserve stale output"
    );

    fs::path mount;

    expect(
        pqnas::storage_pool_runtime_get_named_mount(
            "pool_01",
            &mount
        ) &&
        mount ==
            fs::path(
                "/srv/pqnas/pools/pool_01"
            ),
        "named mount lookup works"
    );

    const auto snapshot =
        pqnas::storage_pool_runtime_snapshot();

    expect(
        snapshot.size() == 1 &&
        snapshot.front().pool_id ==
            "pool_01" &&
        snapshot.front().mount ==
            fs::path(
                "/srv/pqnas/pools/pool_01"
            ),
        "snapshot contains named pool only"
    );

    // Security regression: reset must revoke all previously known named
    // mappings rather than allowing stale pool placement to survive.
    expect(
        pqnas::storage_pool_runtime_reset(
            "/new/default/data"
        ),
        "runtime reset succeeds"
    );

    expect(
        !pqnas::resolve_storage_pool_data_root_strict(
            "pool_01",
            &out
        ),
        "runtime reset removes old named pool"
    );

    expect(
        pqnas::resolve_storage_pool_data_root_strict(
            "",
            &out
        ) &&
        out ==
            fs::path(
                "/new/default/data"
            ),
        "runtime reset replaces default root"
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " storage pool runtime test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage pool runtime tests passed\n";

    return 0;
}
