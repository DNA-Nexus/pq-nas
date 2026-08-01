#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_heartbeat_worker.h"
#include "cluster/node_registry.h"
#include "cluster/node_status.h"

#include <sodium.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;

struct AuditRecord {
    std::string event;
    std::string outcome;
    std::map<std::string, std::string> fields;
};

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
            "pqnas-node-heartbeat-XXXXXX"
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

std::string synthetic_token(
    unsigned char seed
) {
    std::array<
        unsigned char,
        pqnas::cluster::kInternalAuthSecretBytes
    > bytes{};

    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<unsigned char>(
                seed +
                static_cast<unsigned char>(
                    index
                )
            );
    }

    std::array<
        char,
        sodium_base64_ENCODED_LEN(
            pqnas::cluster::
                kInternalAuthSecretBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        )
    > encoded{};

    sodium_bin2base64(
        encoded.data(),
        encoded.size(),
        bytes.data(),
        bytes.size(),
        sodium_base64_VARIANT_URLSAFE_NO_PADDING
    );

    return std::string(encoded.data());
}

pqnas::cluster::NodeStatusSnapshot
make_status(
    const std::string& node_id
) {
    pqnas::cluster::NodeStatusSnapshot status;

    status.node_id = node_id;
    status.hostname = "cellnode-test";
    status.server_version = "1.2.6";
    status.uptime_s = 1234;

    status.storage.total_bytes = 1000;
    status.storage.used_bytes = 400;
    status.storage.free_bytes = 600;
    status.storage.fstype = "ext4";

    status.users.total = 5;
    status.users.enabled = 4;

    status.timestamp =
        "2026-07-28T08:00:00Z";

    return status;
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium_init failed\n";
        return 1;
    }

    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(directory, error);
    };

    std::string error;

    pqnas::cluster::NodeCredentialRegistry
        credential_registry;

    expect(
        credential_registry.load(
            (
                directory /
                "node_credentials"
            ).string(),
            &error
        ),
        "load heartbeat credential registry: " +
            error
    );

    pqnas::cluster::NodeRegistry registry;

    expect(
        registry.load(
            (
                directory /
                "node_registry.json"
            ).string(),
            &error
        ),
        "load heartbeat registry: " + error
    );

    const auto add_node =
        [&](
            const std::string& node_id,
            bool enabled
        ) {
            error.clear();

            expect(
                registry.upsert_configured_node(
                    node_id,
                    "https://" +
                        node_id +
                        ".internal",
                    enabled,
                    "2026-07-28T07:59:00Z",
                    &error
                ),
                "add " + node_id + ": " + error
            );
        };

    add_node("node-01", true);
    add_node("node-02", false);
    add_node("node-03", true);
    add_node("node-04", true);
    add_node("node-05", true);
    add_node("node-06", true);

    std::map<std::string, std::string>
        node_tokens;

    const auto add_credential =
        [&](const std::string& node_id,
            unsigned char seed) {
            std::string token =
                synthetic_token(seed);

            node_tokens[node_id] =
                token;

            error.clear();

            expect(
                credential_registry.upsert(
                    node_id,
                    token,
                    &error
                ) ==
                    pqnas::cluster::
                        NodeCredentialRegistryResult::
                            Ok,
                "add heartbeat credential " +
                    node_id +
                    ": " +
                    error
            );

            pqnas::cluster::
                clear_internal_auth_token_text(
                    &token
                );
        };

    add_credential("node-01", 1);
    add_credential("node-02", 33);
    add_credential("node-03", 65);
    add_credential("node-04", 97);
    add_credential("node-05", 129);

    // node-06 intentionally has no credential. It must fail before fetch().
    std::map<std::string, int> fetch_calls;
    std::vector<AuditRecord> audits;
    bool authorization_valid = true;

    pqnas::cluster::NodeHeartbeatWorkerConfig
        config;

    config.registry = &registry;
    config.credential_registry =
        &credential_registry;

    config.coordination_mutex =
        std::make_shared<std::mutex>();

    config.now_iso_utc =
        []() {
            return std::string(
                "2026-07-28T08:00:01Z"
            );
        };

    config.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<std::string, std::string>& fields) {
            audits.push_back(
                AuditRecord{
                    event,
                    outcome,
                    fields
                }
            );
        };

    config.interval =
        std::chrono::hours(1);

    config.fetch =
        [&](
            const pqnas::cluster::
                NodeRegistryRecord& node,
            const std::string& authorization,
            std::string* body,
            std::string* fetch_error
        ) -> bool {
            ++fetch_calls[node.node_id];

            const auto expected_token =
                node_tokens.find(
                    node.node_id
                );

            authorization_valid =
                authorization_valid &&
                expected_token !=
                    node_tokens.end() &&
                authorization ==
                    "Bearer " +
                    expected_token->second;

            if (fetch_error) {
                fetch_error->clear();
            }

            if (!body) {
                return false;
            }

            body->clear();

            if (node.node_id == "node-01") {
                *body =
                    pqnas::cluster::
                        node_status_to_json(
                            make_status("node-01")
                        ).dump();

                return true;
            }

            if (node.node_id == "node-03") {
                if (fetch_error) {
                    *fetch_error =
                        "synthetic connection failure";
                }

                return false;
            }

            if (node.node_id == "node-04") {
                *body =
                    pqnas::cluster::
                        node_status_to_json(
                            make_status("node-other")
                        ).dump();

                return true;
            }

            if (node.node_id == "node-05") {
                body->assign(
                    pqnas::cluster::
                        kNodeHeartbeatMaxResponseBytes +
                    1,
                    'x'
                );

                return true;
            }

            return false;
        };

    pqnas::cluster::NodeHeartbeatWorker
        worker(config);

    const pqnas::cluster::NodeHeartbeatPollResult
        poll_result =
            worker.poll_once();

    expect(
        poll_result.checked == 5,
        "five enabled nodes should be checked"
    );

    expect(
        poll_result.succeeded == 1,
        "one valid heartbeat should succeed"
    );

    expect(
        poll_result.failed == 4,
        "four invalid heartbeats should fail"
    );

    expect(
        poll_result.skipped_disabled == 1,
        "disabled node should be skipped"
    );

    expect(
        poll_result.online_transitions == 1,
        "first valid heartbeat should create one online transition"
    );

    expect(
        poll_result.offline_transitions == 4,
        "first failures should create four offline transitions"
    );

    expect(
        audits.size() == 5,
        "only real health transitions should be audited"
    );

    std::size_t online_audits = 0;
    std::size_t offline_audits = 0;

    const std::map<std::string, std::string>
        expected_offline_reasons{
            {"node-03", "transport_error"},
            {"node-04", "node_id_mismatch"},
            {"node-05", "response_too_large"},
            {"node-06", "credential_unavailable"}
        };

    for (const AuditRecord& audit : audits) {
        expect(
            audit.outcome == "ok",
            "health transition audit outcome"
        );

        expect(
            audit.fields.size() == 3 &&
            audit.fields.count("node_id") == 1 &&
            audit.fields.count("state") == 1 &&
            audit.fields.count("reason_code") == 1,
            "health audit should contain only bounded fields"
        );

        if (audit.event == "cluster.node_online") {
            ++online_audits;

            expect(
                audit.fields.at("node_id") == "node-01" &&
                audit.fields.at("state") == "online" &&
                audit.fields.at("reason_code") ==
                    "heartbeat_ok",
                "online audit fields"
            );
        } else if (
            audit.event == "cluster.node_offline"
        ) {
            ++offline_audits;

            const std::string node_id =
                audit.fields.at("node_id");

            const auto expected =
                expected_offline_reasons.find(node_id);

            expect(
                expected !=
                    expected_offline_reasons.end() &&
                audit.fields.at("state") ==
                    "offline" &&
                expected !=
                    expected_offline_reasons.end() &&
                audit.fields.at("reason_code") ==
                    expected->second,
                "offline audit fields and reason category"
            );
        } else {
            expect(
                false,
                "unexpected heartbeat audit event"
            );
        }
    }

    expect(
        online_audits == 1 &&
        offline_audits == 4,
        "expected online and offline audit counts"
    );

    expect(
        authorization_valid,
        "every fetch should receive the correct bearer value"
    );

    expect(
        fetch_calls["node-02"] == 0,
        "disabled node must not be fetched"
    );

    expect(
        fetch_calls["node-06"] == 0,
        "credentialless node must fail before network"
    );

    const auto node_01 =
        registry.get("node-01");

    expect(
        node_01.has_value() &&
        node_01->health == "online" &&
        node_01->hostname == "cellnode-test" &&
        node_01->last_seen_at ==
            "2026-07-28T08:00:01Z",
        "valid heartbeat should update online status"
    );

    const auto node_02 =
        registry.get("node-02");

    expect(
        node_02.has_value() &&
        node_02->health == "disabled",
        "disabled node should remain disabled"
    );

    for (const std::string node_id : {
             "node-03",
             "node-04",
             "node-05",
             "node-06"
         }) {
        const auto record =
            registry.get(node_id);

        expect(
            record.has_value() &&
            record->health == "offline",
            node_id +
                " should be marked offline"
        );
    }

    const std::size_t audits_before_repeat =
        audits.size();

    const pqnas::cluster::NodeHeartbeatPollResult
        repeated_poll =
            worker.poll_once();

    expect(
        repeated_poll.online_transitions == 0 &&
        repeated_poll.offline_transitions == 0,
        "unchanged health states must not create transitions"
    );

    expect(
        audits.size() == audits_before_repeat,
        "repeated heartbeat results must not duplicate audit events"
    );

    error.clear();

    expect(
        worker.start(&error),
        "heartbeat worker should start: " + error
    );

    worker.stop_and_join();

    expect(
        !worker.running(),
        "heartbeat worker should stop and join"
    );


    // Verify that a heartbeat started for an older node configuration cannot
    // update or mark offline the replacement configuration.
    pqnas::cluster::NodeRegistry
        stale_registry;

    error.clear();

    expect(
        stale_registry.load(
            (
                directory /
                "stale_node_registry.json"
            ).string(),
            &error
        ),
        "load stale-observation registry: " +
            error
    );

    for (const std::string& node_id : {
             std::string("node-stale-status"),
             std::string("node-stale-failure")
         }) {
        error.clear();

        expect(
            stale_registry.upsert_configured_node(
                node_id,
                "https://" +
                    node_id +
                    "-old.internal",
                true,
                "2026-07-28T08:00:00Z",
                &error
            ),
            "add stale test node " +
                node_id +
                ": " +
                error
        );
    }

    add_credential(
        "node-stale-status",
        161
    );

    add_credential(
        "node-stale-failure",
        193
    );

    auto stale_coordination_mutex =
        std::make_shared<std::mutex>();

    std::vector<AuditRecord>
        stale_audits;

    pqnas::cluster::NodeHeartbeatWorkerConfig
        stale_config;

    stale_config.registry =
        &stale_registry;

    stale_config.credential_registry =
        &credential_registry;

    stale_config.coordination_mutex =
        stale_coordination_mutex;

    stale_config.interval =
        std::chrono::hours(1);

    stale_config.now_iso_utc =
        []() {
            return std::string(
                "2026-07-28T08:00:03Z"
            );
        };

    stale_config.audit_emit =
        [&](const std::string& event,
            const std::string& outcome,
            const std::map<
                std::string,
                std::string
            >& fields) {
            stale_audits.push_back(
                AuditRecord{
                    event,
                    outcome,
                    fields
                }
            );
        };

    stale_config.fetch =
        [&](
            const pqnas::cluster::
                NodeRegistryRecord& node,
            const std::string& authorization,
            std::string* body,
            std::string* fetch_error
        ) -> bool {
            const auto expected_token =
                node_tokens.find(
                    node.node_id
                );

            expect(
                expected_token !=
                    node_tokens.end() &&
                authorization ==
                    "Bearer " +
                    expected_token->second,
                "stale test per-node bearer authorization"
            );

            if (body) {
                body->clear();
            }

            if (fetch_error) {
                fetch_error->clear();
            }

            // Simulate node administration completing while the network
            // request is in flight. The worker must compare the fetched
            // configuration snapshot before mutating registry state.
            {
                std::lock_guard<std::mutex>
                    coordination_lock(
                        *stale_coordination_mutex
                    );

                std::string update_error;

                expect(
                    stale_registry.upsert_configured_node(
                        node.node_id,
                        "https://" +
                            node.node_id +
                            "-new.internal",
                        true,
                        "2026-07-28T08:00:02Z",
                        &update_error
                    ),
                    "replace stale test configuration: " +
                        update_error
                );
            }

            if (node.node_id ==
                "node-stale-status") {
                if (!body) {
                    return false;
                }

                *body =
                    pqnas::cluster::
                        node_status_to_json(
                            make_status(
                                node.node_id
                            )
                        ).dump();

                return true;
            }

            if (fetch_error) {
                *fetch_error =
                    "synthetic stale transport failure";
            }

            return false;
        };

    pqnas::cluster::NodeHeartbeatWorker
        stale_worker(
            stale_config
        );

    const auto stale_poll =
        stale_worker.poll_once();

    expect(
        stale_poll.checked == 2 &&
        stale_poll.succeeded == 0 &&
        stale_poll.failed == 2,
        "stale observations should be discarded"
    );

    expect(
        stale_poll.online_transitions == 0 &&
        stale_poll.offline_transitions == 0 &&
        stale_audits.empty(),
        "stale observations must not create health transitions"
    );

    for (const std::string& node_id : {
             std::string("node-stale-status"),
             std::string("node-stale-failure")
         }) {
        const auto current =
            stale_registry.get(
                node_id
            );

        expect(
            current.has_value() &&
            current->api_base_url ==
                "https://" +
                    node_id +
                    "-new.internal" &&
            current->health == "unknown" &&
            current->last_seen_at.empty(),
            "replacement node configuration must remain untouched: " +
                node_id
        );
    }

    auto invalid_config = config;
    invalid_config.credential_registry =
        nullptr;

    pqnas::cluster::NodeHeartbeatWorker
        invalid_worker(invalid_config);

    error.clear();

    expect(
        !invalid_worker.start(&error),
        "worker must reject missing credential registry"
    );

    auto missing_coordination_config =
        config;

    missing_coordination_config
        .coordination_mutex.reset();

    pqnas::cluster::NodeHeartbeatWorker
        missing_coordination_worker(
            missing_coordination_config
        );

    error.clear();

    expect(
        !missing_coordination_worker.start(
            &error
        ),
        "worker must reject missing coordination mutex"
    );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " heartbeat worker test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node heartbeat worker tests passed\n";

    return 0;
}
