#include "node_heartbeat_worker.h"

#include "internal_auth.h"
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
    const std::string& node_id,
    const std::string& reason_code
) {
    if (!config.registry) {
        return false;
    }

    bool transitioned_offline = false;
    std::string ignored_error;

    if (!config.registry->mark_offline(
            node_id,
            &ignored_error,
            &transitioned_offline) ||
        !transitioned_offline) {
        return false;
    }

    emit_transition_best_effort(
        config,
        "cluster.node_offline",
        node_id,
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

    if (!config_.credential ||
        !config_.credential->configured()) {
        set_error(
            error,
            "heartbeat credential is not configured"
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
        [&](const std::string& node_id,
            const std::string& reason_code) {
            if (mark_offline_transition_best_effort(
                    config_,
                    node_id,
                    reason_code)) {
                ++result.offline_transitions;
            }
        };

    std::string authorization;
    SensitiveStringWiper authorization_wiper(
        authorization
    );

    if (!make_internal_bearer_authorization(
            *config_.credential,
            &authorization)) {
        for (const NodeRegistryRecord& node : nodes) {
            if (!node.enabled) {
                ++result.skipped_disabled;
                continue;
            }

            ++result.checked;
            ++result.failed;

            record_offline(
                node.node_id,
                "credential_unavailable"
            );
        }

        return result;
    }

    for (const NodeRegistryRecord& node : nodes) {
        if (stop_requested()) {
            break;
        }

        if (!node.enabled) {
            ++result.skipped_disabled;
            continue;
        }

        ++result.checked;

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
                node.node_id,
                "transport_error"
            );

            continue;
        }

        if (response_body.empty()) {
            ++result.failed;

            record_offline(
                node.node_id,
                "empty_response"
            );

            continue;
        }

        if (response_body.size() >
            kNodeHeartbeatMaxResponseBytes) {
            ++result.failed;

            record_offline(
                node.node_id,
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
                node.node_id,
                "invalid_response"
            );

            continue;
        }

        // Security: bind the authenticated response to the exact configured
        // node. A valid response from one node must never update another.
        if (status.node_id != node.node_id) {
            ++result.failed;

            record_offline(
                node.node_id,
                "node_id_mismatch"
            );

            continue;
        }

        std::string received_at;

        try {
            received_at =
                config_.now_iso_utc();
        } catch (...) {
            received_at.clear();
        }

        if (received_at.empty()) {
            ++result.failed;

            record_offline(
                node.node_id,
                "clock_error"
            );

            continue;
        }

        bool transitioned_online = false;
        std::string registry_error;

        if (!config_.registry->apply_status(
                status,
                received_at,
                &registry_error,
                &transitioned_online)) {
            ++result.failed;

            record_offline(
                node.node_id,
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
