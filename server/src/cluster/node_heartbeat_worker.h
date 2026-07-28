#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace pqnas::cluster {

class InternalAuthCredential;
class NodeRegistry;
struct NodeRegistryRecord;

inline constexpr std::size_t
    kNodeHeartbeatMaxResponseBytes = 32 * 1024;

using NodeHeartbeatFetch =
    std::function<bool(
        const NodeRegistryRecord& node,
        const std::string& authorization,
        std::string* response_body,
        std::string* error
    )>;

using NodeHeartbeatAuditEmit =
    std::function<void(
        const std::string& event,
        const std::string& outcome,
        const std::map<std::string, std::string>& fields
    )>;

struct NodeHeartbeatWorkerConfig {
    NodeRegistry* registry = nullptr;
    const InternalAuthCredential* credential = nullptr;

    NodeHeartbeatFetch fetch;
    NodeHeartbeatAuditEmit audit_emit;

    std::function<std::string()>
        now_iso_utc;

    std::chrono::milliseconds interval{
        std::chrono::seconds(30)
    };
};

struct NodeHeartbeatPollResult {
    std::size_t checked = 0;
    std::size_t succeeded = 0;
    std::size_t failed = 0;
    std::size_t skipped_disabled = 0;

    std::size_t online_transitions = 0;
    std::size_t offline_transitions = 0;
};

class NodeHeartbeatWorker {
public:
    explicit NodeHeartbeatWorker(
        NodeHeartbeatWorkerConfig config
    );

    ~NodeHeartbeatWorker();

    NodeHeartbeatWorker(
        const NodeHeartbeatWorker&
    ) = delete;

    NodeHeartbeatWorker& operator=(
        const NodeHeartbeatWorker&
    ) = delete;

    bool start(std::string* error);
    void stop_and_join();

    bool running() const;

    // Exposed for deterministic tests and explicit one-shot refreshes.
    NodeHeartbeatPollResult poll_once();

private:
    bool config_valid(std::string* error) const;
    bool stop_requested() const;
    void run();

    NodeHeartbeatWorkerConfig config_;

    mutable std::mutex state_mutex_;
    std::condition_variable wake_condition_;
    std::thread worker_thread_;

    bool stop_requested_ = false;
    bool running_ = false;
};

} // namespace pqnas::cluster
