#include "cluster/node_registry.h"

#include "cluster/node_status.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-node-registry-test-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(writable.data());

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

} // namespace

int main() {
    const fs::path test_directory =
        make_test_directory();

    if (test_directory.empty()) {
        std::cerr
            << "FAIL: could not create test directory\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(test_directory, error);
    };

    const fs::path registry_path =
        test_directory / "node_registry.json";

    pqnas::cluster::NodeRegistry registry;
    std::string error;

    expect(
        registry.load(
            registry_path.string(),
            &error
        ),
        "missing registry should load as empty: " + error
    );

    expect(
        registry.snapshot().empty(),
        "missing registry should create empty memory state"
    );

    error.clear();

    expect(
        !registry.upsert_configured_node(
            "../node-01",
            "https://node-01.internal",
            true,
            "2026-07-28T06:00:00Z",
            &error
        ),
        "unsafe node ID must be rejected"
    );

    error.clear();

    expect(
        !registry.upsert_configured_node(
            "node-01",
            "http://node-01.internal",
            true,
            "2026-07-28T06:00:00Z",
            &error
        ),
        "plaintext node URL must be rejected"
    );

    error.clear();

    expect(
        !registry.upsert_configured_node(
            "node-01",
            "https://node-01.internal/status",
            true,
            "2026-07-28T06:00:00Z",
            &error
        ),
        "node URL with a path must be rejected"
    );

    expect(
        registry.upsert_configured_node(
            "node-02",
            "https://node-02.internal/",
            true,
            "2026-07-28T06:01:00Z",
            &error
        ),
        "add node-02: " + error
    );

    error.clear();

    expect(
        registry.upsert_configured_node(
            "node-01",
            "https://node-01.internal:8443",
            true,
            "2026-07-28T06:02:00Z",
            &error
        ),
        "add node-01: " + error
    );

    error.clear();

    expect(
        !registry.upsert_configured_node(
            "node-03",
            "https://node-02.internal",
            true,
            "2026-07-28T06:03:00Z",
            &error
        ),
        "duplicate node URL must be rejected"
    );

    const auto snapshot = registry.snapshot();

    expect(
        snapshot.size() == 2,
        "registry should contain two nodes"
    );

    expect(
        snapshot.size() >= 2 &&
        snapshot[0].node_id == "node-01" &&
        snapshot[1].node_id == "node-02",
        "snapshot should be sorted by node ID"
    );

    struct stat registry_stats {};

    expect(
        ::stat(
            registry_path.c_str(),
            &registry_stats
        ) == 0,
        "registry file should exist"
    );

    expect(
        (registry_stats.st_mode & 0077) == 0,
        "registry file must not allow group or other access"
    );

    json persisted;

    try {
        std::ifstream input(registry_path);
        input >> persisted;
    } catch (...) {
        expect(false, "persisted registry should contain JSON");
    }

    expect(
        persisted["version"] ==
            pqnas::cluster::
                kNodeRegistrySchemaVersion,
        "persisted schema version"
    );

    expect(
        persisted["nodes"].is_array() &&
        persisted["nodes"].size() == 2,
        "persisted node array"
    );

    expect(
        persisted["nodes"][0]["node_id"] ==
            "node-01" &&
        persisted["nodes"][1]["node_id"] ==
            "node-02",
        "persisted nodes should be deterministic"
    );

    pqnas::cluster::NodeRegistry reloaded;

    error.clear();

    expect(
        reloaded.load(
            registry_path.string(),
            &error
        ),
        "reload persisted registry: " + error
    );

    const auto node_one =
        reloaded.get("node-01");

    expect(
        node_one.has_value(),
        "reloaded node-01 should exist"
    );

    expect(
        node_one.has_value() &&
        node_one->api_base_url ==
            "https://node-01.internal:8443",
        "reloaded node URL"
    );

    pqnas::cluster::NodeStatusSnapshot status;
    status.node_id = "node-01";
    status.hostname = "cellnode2";
    status.server_version = "1.2.6";
    status.uptime_s = 1234;
    status.storage.total_bytes = 1000;
    status.storage.used_bytes = 400;
    status.storage.free_bytes = 600;
    status.storage.fstype = "ext4";
    status.users.total = 7;
    status.users.enabled = 6;
    status.timestamp =
        "2026-07-28T06:04:00Z";

    error.clear();

    expect(
        reloaded.apply_status(
            status,
            "2026-07-28T06:04:01Z",
            &error
        ),
        "apply node status: " + error
    );

    const auto observed =
        reloaded.get("node-01");

    expect(
        observed.has_value() &&
        observed->health == "online",
        "heartbeat should mark node online"
    );

    expect(
        observed.has_value() &&
        observed->last_seen_at ==
            "2026-07-28T06:04:01Z" &&
        observed->node_reported_at ==
            "2026-07-28T06:04:00Z",
        "Director and node timestamps should remain distinct"
    );

    expect(
        observed.has_value() &&
        observed->storage_total_bytes == 1000 &&
        observed->storage_used_bytes == 400 &&
        observed->storage_free_bytes == 600,
        "observed storage counters"
    );

    pqnas::cluster::NodeStatusSnapshot unknown_status =
        status;

    unknown_status.node_id = "node-99";

    error.clear();

    expect(
        !reloaded.apply_status(
            unknown_status,
            "2026-07-28T06:05:00Z",
            &error
        ),
        "heartbeat must not create an unknown node"
    );

    error.clear();

    expect(
        reloaded.set_enabled(
            "node-01",
            false,
            "2026-07-28T06:06:00Z",
            &error
        ),
        "disable node-01: " + error
    );

    const auto disabled =
        reloaded.get("node-01");

    expect(
        disabled.has_value() &&
        !disabled->enabled &&
        disabled->health == "disabled" &&
        disabled->last_seen_at.empty(),
        "disabled node should clear active health"
    );

    error.clear();

    expect(
        !reloaded.apply_status(
            status,
            "2026-07-28T06:07:00Z",
            &error
        ),
        "disabled node must reject heartbeat updates"
    );

    const auto before_failed_load =
        reloaded.snapshot();

    expect(
        write_text(
            registry_path,
            "{ definitely not JSON\n"
        ),
        "write malformed registry test file"
    );

    error.clear();

    expect(
        !reloaded.load(
            registry_path.string(),
            &error
        ),
        "malformed registry must fail closed"
    );

    expect(
        reloaded.snapshot().size() ==
            before_failed_load.size(),
        "failed load must preserve in-memory state"
    );

    const fs::path real_file =
        test_directory / "real_registry.json";

    expect(
        write_text(
            real_file,
            R"({"version":1,"nodes":[]})"
        ),
        "write symlink target"
    );

    expect(
        ::chmod(real_file.c_str(), 0600) == 0,
        "secure symlink target permissions"
    );

    const fs::path symlink_path =
        test_directory / "registry_link.json";

    std::error_code symlink_error;

    fs::create_symlink(
        real_file.filename(),
        symlink_path,
        symlink_error
    );

    expect(
        !symlink_error,
        "create registry symlink"
    );

    if (!symlink_error) {
        pqnas::cluster::NodeRegistry symlink_registry;

        error.clear();

        expect(
            !symlink_registry.load(
                symlink_path.string(),
                &error
            ),
            "registry symlink must be rejected"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " node registry test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node registry tests passed\n";
    return 0;
}
