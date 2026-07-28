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

void mark_offline_best_effort(
    NodeRegistry* registry,
    const std::string& node_id
) {
    if (!registry) {
        return;
    }

    std::string ignored_error;
    (void)registry->mark_offline(
        node_id,
        &ignored_error
    );
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

            mark_offline_best_effort(
                config_.registry,
                node.node_id
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

        if (!fetched ||
            response_body.empty() ||
            response_body.size() >
                kNodeHeartbeatMaxResponseBytes) {
            ++result.failed;

            mark_offline_best_effort(
                config_.registry,
                node.node_id
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

        // Security: bind the authenticated response to the exact configured
        // node. A valid response from one node must never update another.
        if (!parsed ||
            status.node_id != node.node_id) {
            ++result.failed;

            mark_offline_best_effort(
                config_.registry,
                node.node_id
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

        std::string registry_error;

        if (received_at.empty() ||
            !config_.registry->apply_status(
                status,
                received_at,
                &registry_error)) {
            ++result.failed;

            mark_offline_best_effort(
                config_.registry,
                node.node_id
            );

            continue;
        }

        ++result.succeeded;
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
