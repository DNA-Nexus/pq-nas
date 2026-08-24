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
            &error,
            2000
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

    expect(
        persisted["nodes"][0]["capacity_ceiling_bytes"] ==
            2000 &&
        persisted["nodes"][1]["capacity_ceiling_bytes"] ==
            0,
        "persisted Director-owned capacity ceilings"
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

    expect(
        node_one.has_value() &&
        node_one->capacity_ceiling_bytes == 2000,
        "reloaded Director-owned capacity ceiling"
    );

    // Security/backward compatibility: a schema-v1 registry has no trusted
    // Director capacity envelope. It must load as zero, which makes placement
    // fail closed until an administrator configures the ceiling.
    const fs::path legacy_registry_path =
        test_directory / "legacy_node_registry.json";

    json legacy = persisted;
    legacy["version"] =
        pqnas::cluster::kNodeRegistryLegacySchemaVersion;

    for (auto& node : legacy["nodes"]) {
        node.erase("capacity_ceiling_bytes");
    }

    expect(
        write_text(
            legacy_registry_path,
            legacy.dump(2) + "\n"
        ),
        "write legacy schema-v1 registry"
    );

    // Keep the manually-created security fixture deterministic regardless
    // of the test process umask.
    expect(
        ::chmod(
            legacy_registry_path.c_str(),
            0600
        ) == 0,
        "secure legacy schema-v1 registry fixture"
    );

    pqnas::cluster::NodeRegistry legacy_registry;
    error.clear();

    const bool legacy_loaded =
        legacy_registry.load(
            legacy_registry_path.string(),
            &error
        );

    expect(
        legacy_loaded,
        "legacy schema-v1 registry should load: " + error
    );

    const auto legacy_node =
        legacy_registry.get("node-01");

    expect(
        legacy_node.has_value() &&
        legacy_node->capacity_ceiling_bytes == 0,
        "legacy node capacity must migrate fail closed"
    );

    pqnas::cluster::NodeStatusSnapshot status;

    // Rolling-upgrade regression: a legacy Storage heartbeat must remain v1
    // after NodeRegistry persistence; it must not be rewritten as current v2.
    status.protocol_version =
        pqnas::cluster::
            kNodeStatusProtocolVersionLegacy;

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
        observed->protocol_version ==
            pqnas::cluster::
                kNodeStatusProtocolVersionLegacy,
        "registry preserves exact legacy heartbeat protocol version"
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

    expect(
        observed.has_value() &&
        observed->capacity_ceiling_bytes == 2000,
        "heartbeat must preserve Director-owned capacity ceiling"
    );

    pqnas::cluster::NodeStatusSnapshot invalid_protocol_status =
        status;

    invalid_protocol_status.protocol_version =
        pqnas::cluster::
            kNodeStatusProtocolVersionCurrent + 1;

    error.clear();

    expect(
        !reloaded.apply_status(
            invalid_protocol_status,
            "2026-07-28T06:04:02Z",
            &error
        ),
        "registry rejects unsupported heartbeat protocol version"
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

    {
        const fs::path rollback_directory =
            test_directory / "rollback";

        const fs::path rollback_registry_path =
            rollback_directory / "node_registry.json";

        pqnas::cluster::NodeRegistry
            rollback_registry;

        error.clear();

        expect(
            rollback_registry.load(
                rollback_registry_path.string(),
                &error
            ),
            "load empty rollback registry: " + error
        );

        error.clear();

        expect(
            rollback_registry.upsert_configured_node(
                "node-rollback",
                "https://node-rollback.internal",
                true,
                "2026-07-28T06:08:00Z",
                &error
            ),
            "create rollback test node: " + error
        );

        const auto before_failed_write =
            rollback_registry.snapshot();

        std::error_code rollback_error;

        const bool removed_registry =
            fs::remove(
                rollback_registry_path,
                rollback_error
            );

        expect(
            removed_registry && !rollback_error,
            "remove rollback registry file"
        );

        rollback_error.clear();

        const bool removed_directory =
            fs::remove(
                rollback_directory,
                rollback_error
            );

        expect(
            removed_directory && !rollback_error,
            "remove rollback registry directory"
        );

        // Replace the expected directory with a regular file so the next
        // persistence operation fails deterministically without relying on
        // filesystem permissions or the test process UID.
        expect(
            write_text(
                rollback_directory,
                "not a directory\n"
            ),
            "create deterministic persistence blocker"
        );

        error.clear();

        expect(
            !rollback_registry.set_enabled(
                "node-rollback",
                false,
                "2026-07-28T06:09:00Z",
                &error
            ),
            "failed persistence must reject mutation"
        );

        const auto after_failed_write =
            rollback_registry.snapshot();

        expect(
            after_failed_write.size() ==
                before_failed_write.size(),
            "failed persistence must preserve registry size"
        );

        expect(
            after_failed_write.size() == 1 &&
            before_failed_write.size() == 1 &&
            after_failed_write[0].node_id ==
                before_failed_write[0].node_id &&
            after_failed_write[0].enabled ==
                before_failed_write[0].enabled &&
            after_failed_write[0].health ==
                before_failed_write[0].health &&
            after_failed_write[0].updated_at ==
                before_failed_write[0].updated_at,
            "failed persistence must preserve in-memory record"
        );
    }

    {
        const fs::path offline_path =
            test_directory /
            "offline_registry.json";

        pqnas::cluster::NodeRegistry
            offline_registry;

        error.clear();

        expect(
            offline_registry.load(
                offline_path.string(),
                &error
            ),
            "load offline test registry: " + error
        );

        error.clear();

        expect(
            offline_registry.upsert_configured_node(
                "node-offline",
                "https://node-offline.internal",
                true,
                "2026-07-28T07:10:00Z",
                &error
            ),
            "create offline test node: " + error
        );

        pqnas::cluster::NodeStatusSnapshot
            online_status;

        online_status.node_id = "node-offline";
        online_status.hostname = "cellnode-offline";
        online_status.server_version = "1.2.6";
        online_status.uptime_s = 500;
        online_status.storage.total_bytes = 1000;
        online_status.storage.used_bytes = 400;
        online_status.storage.free_bytes = 600;
        online_status.storage.fstype = "ext4";
        online_status.users.total = 4;
        online_status.users.enabled = 3;
        online_status.timestamp =
            "2026-07-28T07:11:00Z";

        error.clear();

        bool transitioned_online = false;

        expect(
            offline_registry.apply_status(
                online_status,
                "2026-07-28T07:11:01Z",
                &error,
                &transitioned_online
            ),
            "apply online status before offline transition: " +
                error
        );

        expect(
            transitioned_online,
            "unknown node should report transition to online"
        );

        const auto online_record =
            offline_registry.get("node-offline");

        expect(
            online_record.has_value() &&
            online_record->health == "online",
            "heartbeat should first mark node online"
        );

        error.clear();

        bool transitioned_offline = false;

        expect(
            offline_registry.mark_offline(
                "node-offline",
                &error,
                &transitioned_offline
            ),
            "mark heartbeat failure offline: " + error
        );

        expect(
            transitioned_offline,
            "online node should report transition to offline"
        );

        const auto offline_record =
            offline_registry.get("node-offline");

        expect(
            offline_record.has_value() &&
            offline_record->health == "offline",
            "failed heartbeat should mark node offline"
        );

        expect(
            offline_record.has_value() &&
            online_record.has_value() &&
            offline_record->last_seen_at ==
                online_record->last_seen_at &&
            offline_record->hostname ==
                online_record->hostname &&
            offline_record->storage_total_bytes ==
                online_record->storage_total_bytes,
            "offline transition should preserve last successful status"
        );

        error.clear();
        transitioned_offline = true;

        expect(
            offline_registry.mark_offline(
                "node-offline",
                &error,
                &transitioned_offline
            ),
            "repeated offline transition should be idempotent"
        );

        expect(
            !transitioned_offline,
            "already-offline node must not report another transition"
        );

        pqnas::cluster::NodeRegistry
            offline_reloaded;

        error.clear();

        expect(
            offline_reloaded.load(
                offline_path.string(),
                &error
            ),
            "reload persisted offline registry: " + error
        );

        const auto persisted_offline =
            offline_reloaded.get("node-offline");

        expect(
            persisted_offline.has_value() &&
            persisted_offline->health == "offline",
            "offline state should persist"
        );
    }

    {
        const fs::path insert_only_path =
            test_directory /
            "insert_only_registry.json";

        pqnas::cluster::NodeRegistry
            insert_only_registry;

        error.clear();

        expect(
            insert_only_registry.load(
                insert_only_path.string(),
                &error
            ),
            "load insert-only registry: " + error
        );

        error.clear();

        expect(
            insert_only_registry.
                insert_configured_node_if_absent(
                    "../node-insert",
                    "https://node-insert.internal",
                    true,
                    "2026-07-28T08:00:00Z",
                    &error
                ) ==
                pqnas::cluster::
                    NodeRegistryInsertResult::
                        InvalidInput,
            "insert-only mutation must reject unsafe node ID"
        );

        error.clear();

        expect(
            insert_only_registry.
                insert_configured_node_if_absent(
                    "node-insert-a",
                    "http://node-insert-a.internal",
                    true,
                    "2026-07-28T08:00:00Z",
                    &error
                ) ==
                pqnas::cluster::
                    NodeRegistryInsertResult::
                        InvalidInput,
            "insert-only mutation must reject plaintext URL"
        );

        error.clear();

        expect(
            insert_only_registry.
                insert_configured_node_if_absent(
                    "node-insert-a",
                    "https://node-insert-a.internal/",
                    true,
                    "2026-07-28T08:01:00Z",
                    &error
                ) ==
                pqnas::cluster::
                    NodeRegistryInsertResult::Ok,
            "insert first configured node: " + error
        );

        const auto inserted_a =
            insert_only_registry.get(
                "node-insert-a"
            );

        expect(
            inserted_a.has_value() &&
            inserted_a->api_base_url ==
                "https://node-insert-a.internal" &&
            inserted_a->enabled &&
            inserted_a->health == "unknown" &&
            inserted_a->added_at ==
                "2026-07-28T08:01:00Z" &&
            inserted_a->updated_at ==
                "2026-07-28T08:01:00Z",
            "first insert must persist canonical immutable identity metadata"
        );

        error.clear();

        expect(
            insert_only_registry.
                insert_configured_node_if_absent(
                    "node-insert-a",
                    "https://replacement.internal",
                    false,
                    "2026-07-28T08:02:00Z",
                    &error
                ) ==
                pqnas::cluster::
                    NodeRegistryInsertResult::
                        DuplicateNodeId,
            "insert-only mutation must reject duplicate node ID"
        );

        const auto after_id_conflict =
            insert_only_registry.get(
                "node-insert-a"
            );

        expect(
            after_id_conflict.has_value() &&
            after_id_conflict->api_base_url ==
                "https://node-insert-a.internal" &&
            after_id_conflict->enabled &&
            after_id_conflict->added_at ==
                "2026-07-28T08:01:00Z" &&
            after_id_conflict->updated_at ==
                "2026-07-28T08:01:00Z",
            "duplicate node ID must preserve the original record"
        );

        error.clear();

        expect(
            insert_only_registry.
                insert_configured_node_if_absent(
                    "node-insert-b",
                    "https://node-insert-a.internal/",
                    true,
                    "2026-07-28T08:03:00Z",
                    &error
                ) ==
                pqnas::cluster::
                    NodeRegistryInsertResult::
                        DuplicateApiBaseUrl,
            "insert-only mutation must reject duplicate canonical URL"
        );

        expect(
            !insert_only_registry.get(
                "node-insert-b"
            ).has_value(),
            "duplicate URL must not create a second node"
        );

        error.clear();

        expect(
            insert_only_registry.
                insert_configured_node_if_absent(
                    "node-insert-b",
                    "https://node-insert-b.internal",
                    false,
                    "2026-07-28T08:04:00Z",
                    &error
                ) ==
                pqnas::cluster::
                    NodeRegistryInsertResult::Ok,
            "insert disabled configured node: " + error
        );

        const auto inserted_b =
            insert_only_registry.get(
                "node-insert-b"
            );

        expect(
            inserted_b.has_value() &&
            !inserted_b->enabled &&
            inserted_b->health == "disabled",
            "disabled insert must begin in disabled health"
        );

        pqnas::cluster::NodeRegistry
            unloaded_insert_registry;

        error.clear();

        expect(
            unloaded_insert_registry.
                insert_configured_node_if_absent(
                    "node-unloaded",
                    "https://node-unloaded.internal",
                    true,
                    "2026-07-28T08:05:00Z",
                    &error
                ) ==
                pqnas::cluster::
                    NodeRegistryInsertResult::
                        PersistFailed,
            "unloaded insert-only registry must report persistence failure"
        );

        expect(
            std::string(
                pqnas::cluster::
                    node_registry_insert_result_code(
                        pqnas::cluster::
                            NodeRegistryInsertResult::
                                InvalidInput
                    )
            ) == "node_registry_invalid_input" &&
            std::string(
                pqnas::cluster::
                    node_registry_insert_result_code(
                        pqnas::cluster::
                            NodeRegistryInsertResult::
                                DuplicateNodeId
                    )
            ) == "node_registry_duplicate_node_id" &&
            std::string(
                pqnas::cluster::
                    node_registry_insert_result_code(
                        pqnas::cluster::
                            NodeRegistryInsertResult::
                                DuplicateApiBaseUrl
                    )
            ) ==
                "node_registry_duplicate_api_base_url" &&
            std::string(
                pqnas::cluster::
                    node_registry_insert_result_code(
                        pqnas::cluster::
                            NodeRegistryInsertResult::
                                PersistFailed
                    )
            ) == "node_registry_persist_failed",
            "stable node insertion reason codes"
        );

        pqnas::cluster::NodeRegistry
            insert_only_reloaded;

        error.clear();

        expect(
            insert_only_reloaded.load(
                insert_only_path.string(),
                &error
            ),
            "reload insert-only registry: " + error
        );

        const auto inserted_snapshot =
            insert_only_reloaded.snapshot();

        expect(
            inserted_snapshot.size() == 2 &&
            inserted_snapshot[0].node_id ==
                "node-insert-a" &&
            inserted_snapshot[1].node_id ==
                "node-insert-b",
            "insert-only records must survive reload deterministically"
        );
    }

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
