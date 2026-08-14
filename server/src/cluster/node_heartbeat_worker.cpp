#include "node_heartbeat_worker.h"

#include "cluster_timestamp.h"
#include "node_credential_registry.h"
#include "node_registry.h"
#include "node_status.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <string>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    )
        : value_(value) {}

    ~SensitiveStringWiper() {
        if (!value_.empty()) {
            // Security: remove the short-lived cluster Authorization value
            // when the polling round finishes.
            sodium_memzero(
                value_.data(),
                value_.size()
            );

            value_.clear();
        }
    }

    SensitiveStringWiper(
        const SensitiveStringWiper&
    ) = delete;

    SensitiveStringWiper& operator=(
        const SensitiveStringWiper&
    ) = delete;

private:
    std::string& value_;
};

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

bool same_configured_node(
    const NodeRegistryRecord& current,
    const NodeRegistryRecord& expected
) {
    // Integrity: treat the node configuration as a compare-and-set token.
    // A heartbeat started for an older URL or configuration must not update
    // the replacement configuration after an admin mutation.
    return
        current.node_id ==
            expected.node_id &&
        current.api_base_url ==
            expected.api_base_url &&
        current.enabled ==
            expected.enabled &&
        current.added_at ==
            expected.added_at &&
        current.updated_at ==
            expected.updated_at;
}

void emit_transition_best_effort(
    const NodeHeartbeatWorkerConfig& config,
    const std::string& event,
    const std::string& node_id,
    const std::string& state,
    const std::string& reason_code
) {
    if (!config.audit_emit) {
        return;
    }

    // Security: audit only stable identifiers and bounded reason categories.
    // Never include bearer values, response bodies, URLs or raw errors.
    const std::map<std::string, std::string> fields{
        {"node_id", node_id},
        {"state", state},
        {"reason_code", reason_code}
    };

    try {
        config.audit_emit(
            event,
            "ok",
            fields
        );
    } catch (...) {
        // Health tracking must remain functional even if audit output fails.
    }
}

bool mark_offline_transition_best_effort(
    const NodeHeartbeatWorkerConfig& config,
    const NodeRegistryRecord& expected_node,
    const std::string& reason_code
) {
    if (!config.registry ||
        !config.coordination_mutex) {
        return false;
    }

    bool transitioned_offline = false;
    std::string ignored_error;

    {
        // TOCTOU protection: compare the current configured node and perform
        // the mutation under the same Director coordination lock.
        std::lock_guard<std::mutex> coordination_lock(
            *config.coordination_mutex
        );

        const auto current =
            config.registry->get(
                expected_node.node_id
            );

        if (!current.has_value() ||
            !same_configured_node(
                *current,
                expected_node
            )) {
            return false;
        }

        if (!config.registry->mark_offline(
                expected_node.node_id,
                &ignored_error,
                &transitioned_offline) ||
            !transitioned_offline) {
            return false;
        }
    }

    emit_transition_best_effort(
        config,
        "cluster.node_offline",
        expected_node.node_id,
        "offline",
        reason_code
    );

    return true;
}

} // namespace

NodeHeartbeatWorker::NodeHeartbeatWorker(
    NodeHeartbeatWorkerConfig config
)
    : config_(std::move(config)) {}

NodeHeartbeatWorker::~NodeHeartbeatWorker() {
    stop_and_join();
}

bool NodeHeartbeatWorker::config_valid(
    std::string* error
) const {
    if (!config_.registry) {
        set_error(
            error,
            "heartbeat registry is missing"
        );
        return false;
    }

    if (!config_.coordination_mutex) {
        set_error(
            error,
            "heartbeat coordination mutex is missing"
        );
        return false;
    }

    if (!config_.credential_registry) {
        set_error(
            error,
            "heartbeat credential registry is missing"
        );
        return false;
    }

    if (!config_.fetch) {
        set_error(
            error,
            "heartbeat fetch callback is missing"
        );
        return false;
    }

    if (!config_.audit_emit) {
        set_error(
            error,
            "heartbeat audit callback is missing"
        );
        return false;
    }

    if (!config_.now_iso_utc) {
        set_error(
            error,
            "heartbeat clock callback is missing"
        );
        return false;
    }

    if (config_.interval.count() <= 0) {
        set_error(
            error,
            "heartbeat interval must be positive"
        );
        return false;
    }

    return true;
}

bool NodeHeartbeatWorker::start(
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!config_valid(error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        state_mutex_
    );

    if (running_ || worker_thread_.joinable()) {
        set_error(
            error,
            "heartbeat worker is already running"
        );
        return false;
    }

    stop_requested_ = false;
    running_ = true;

    try {
        worker_thread_ = std::thread(
            [this]() {
                run();
            }
        );
    } catch (...) {
        running_ = false;
        stop_requested_ = true;

        set_error(
            error,
            "failed to start heartbeat worker"
        );

        return false;
    }

    return true;
}

void NodeHeartbeatWorker::stop_and_join() {
    {
        std::lock_guard<std::mutex> lock(
            state_mutex_
        );

        stop_requested_ = true;
    }

    wake_condition_.notify_all();

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(
            state_mutex_
        );

        running_ = false;
    }
}

bool NodeHeartbeatWorker::running() const {
    std::lock_guard<std::mutex> lock(
        state_mutex_
    );

    return running_;
}

bool NodeHeartbeatWorker::stop_requested() const {
    std::lock_guard<std::mutex> lock(
        state_mutex_
    );

    return stop_requested_;
}

NodeHeartbeatPollResult
NodeHeartbeatWorker::poll_once() {
    NodeHeartbeatPollResult result;

    if (!config_valid(nullptr)) {
        return result;
    }

    const std::vector<NodeRegistryRecord> nodes =
        config_.registry->snapshot();

    const auto record_offline =
        [&](const NodeRegistryRecord& expected_node,
            const std::string& reason_code) {
            if (mark_offline_transition_best_effort(
                    config_,
                    expected_node,
                    reason_code)) {
                ++result.offline_transitions;
            }
        };

    for (const NodeRegistryRecord& node : nodes) {
        if (stop_requested()) {
            break;
        }

        if (!node.enabled) {
            ++result.skipped_disabled;
            continue;
        }

        ++result.checked;

        std::string authorization;
        SensitiveStringWiper authorization_wiper(
            authorization
        );

        std::string credential_error;

        const NodeCredentialRegistryResult
            credential_result =
                config_.credential_registry
                    ->make_bearer_authorization(
                        node.node_id,
                        &authorization,
                        &credential_error
                    );

        // Security: never expose credential storage details through health
        // state, audit output or logs.
        credential_error.clear();

        if (credential_result !=
            NodeCredentialRegistryResult::Ok) {
            ++result.failed;

            record_offline(
                node,
                "credential_unavailable"
            );

            continue;
        }

        // The registry lock is released before network I/O. The bearer is
        // wiped by the iteration-local guard after this node is processed.
        std::string response_body;
        std::string fetch_error;

        bool fetched = false;

        try {
            fetched = config_.fetch(
                node,
                authorization,
                &response_body,
                &fetch_error
            );
        } catch (...) {
            fetched = false;
        }

        if (!fetched) {
            ++result.failed;

            record_offline(
                node,
                "transport_error"
            );

            continue;
        }

        if (response_body.empty()) {
            ++result.failed;

            record_offline(
                node,
                "empty_response"
            );

            continue;
        }

        if (response_body.size() >
            kNodeHeartbeatMaxResponseBytes) {
            ++result.failed;

            record_offline(
                node,
                "response_too_large"
            );

            continue;
        }

        const json response_json =
            json::parse(
                response_body,
                nullptr,
                false
            );

        NodeStatusSnapshot status;
        std::string parse_error;

        bool parsed = false;

        try {
            parsed =
                !response_json.is_discarded() &&
                parse_node_status_json(
                    response_json,
                    &status,
                    &parse_error
                );
        } catch (...) {
            parsed = false;
        }

        if (!parsed) {
            ++result.failed;

            record_offline(
                node,
                "invalid_response"
            );

            continue;
        }

        // Security: bind the authenticated response to the exact configured
        // node. A valid response from one node must never update another.
        if (status.node_id != node.node_id) {
            ++result.failed;

            record_offline(
                node,
                "node_id_mismatch"
            );

            continue;
        }

        std::string received_at;

        try {
            // Reliability/security: the shared runtime clock includes
            // milliseconds, while cluster persistence intentionally accepts
            // only canonical UTC seconds.
            received_at =
                canonicalize_cluster_runtime_timestamp(
                    config_.now_iso_utc()
                );
        } catch (...) {
            received_at.clear();
        }

        if (received_at.empty()) {
            ++result.failed;

            record_offline(
                node,
                "clock_error"
            );

            continue;
        }

        bool transitioned_online = false;
        bool applied = false;
        bool configuration_stale = false;
        std::string registry_error;

        {
            // TOCTOU protection: re-check the exact node configuration and
            // apply the observation while holding the same coordination lock
            // used by node administration and placement decisions.
            std::lock_guard<std::mutex> coordination_lock(
                *config_.coordination_mutex
            );

            const auto current =
                config_.registry->get(
                    node.node_id
                );

            if (!current.has_value() ||
                !same_configured_node(
                    *current,
                    node
                )) {
                configuration_stale = true;
            } else {
                applied =
                    config_.registry->apply_status(
                        status,
                        received_at,
                        &registry_error,
                        &transitioned_online
                    );
            }
        }

        if (configuration_stale) {
            // The observation belongs to an older node configuration. Treat it
            // as a failed poll, but never mark the replacement configuration
            // offline and never apply the stale response.
            ++result.failed;
            continue;
        }

        if (!applied) {
            ++result.failed;

            record_offline(
                node,
                "registry_update_failed"
            );

            continue;
        }

        ++result.succeeded;

        if (transitioned_online) {
            ++result.online_transitions;

            emit_transition_best_effort(
                config_,
                "cluster.node_online",
                node.node_id,
                "online",
                "heartbeat_ok"
            );
        }
    }

    return result;
}

void NodeHeartbeatWorker::run() {
    while (!stop_requested()) {
        (void)poll_once();

        std::unique_lock<std::mutex> lock(
            state_mutex_
        );

        if (wake_condition_.wait_for(
                lock,
                config_.interval,
                [this]() {
                    return stop_requested_;
                })) {
            break;
        }
    }

    std::lock_guard<std::mutex> lock(
        state_mutex_
    );

    running_ = false;
}

} // namespace pqnas::cluster
