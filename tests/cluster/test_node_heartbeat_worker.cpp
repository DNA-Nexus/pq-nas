#include "cluster/internal_auth.h"
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
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

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

bool write_file_0600(
    const fs::path& path,
    const std::string& body
) {
    {
        std::ofstream output(
            path,
            std::ios::binary |
            std::ios::trunc
        );

        if (!output.good()) {
            return false;
        }

        output.write(
            body.data(),
            static_cast<std::streamsize>(
                body.size()
            )
        );

        if (!output.good()) {
            return false;
        }
    }

    return ::chmod(path.c_str(), 0600) == 0;
}

std::string synthetic_token() {
    std::array<
        unsigned char,
        pqnas::cluster::kInternalAuthSecretBytes
    > bytes{};

    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<unsigned char>(
                index + 1
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

    const std::string token =
        synthetic_token();

    const fs::path credential_path =
        directory / "cluster-token";

    expect(
        write_file_0600(
            credential_path,
            token + "\n"
        ),
        "write cluster credential"
    );

    pqnas::cluster::InternalAuthCredential
        credential;

    std::string error;

    expect(
        pqnas::cluster::
            load_internal_auth_credential_file(
                credential_path.string(),
                &credential,
                &error
            ),
        "load cluster credential: " + error
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

    std::map<std::string, int> fetch_calls;
    bool authorization_valid = true;

    pqnas::cluster::NodeHeartbeatWorkerConfig
        config;

    config.registry = &registry;
    config.credential = &credential;

    config.now_iso_utc =
        []() {
            return std::string(
                "2026-07-28T08:00:01Z"
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

            authorization_valid =
                authorization_valid &&
                pqnas::cluster::
                    verify_internal_bearer_authorization(
                        authorization,
                        credential
                    );

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
        poll_result.checked == 4,
        "four enabled nodes should be checked"
    );

    expect(
        poll_result.succeeded == 1,
        "one valid heartbeat should succeed"
    );

    expect(
        poll_result.failed == 3,
        "three invalid heartbeats should fail"
    );

    expect(
        poll_result.skipped_disabled == 1,
        "disabled node should be skipped"
    );

    expect(
        authorization_valid,
        "every fetch should receive the correct bearer value"
    );

    expect(
        fetch_calls["node-02"] == 0,
        "disabled node must not be fetched"
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
             "node-05"
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

    pqnas::cluster::InternalAuthCredential
        unconfigured_credential;

    auto invalid_config = config;
    invalid_config.credential =
        &unconfigured_credential;

    pqnas::cluster::NodeHeartbeatWorker
        invalid_worker(invalid_config);

    error.clear();

    expect(
        !invalid_worker.start(&error),
        "worker must reject unconfigured credential"
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
