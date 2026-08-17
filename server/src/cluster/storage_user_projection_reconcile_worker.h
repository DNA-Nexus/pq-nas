#pragma once

#include "cluster/storage_user_projection_reconciler.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

namespace pqnas::cluster {

using StorageUserProjectionReconcileWorkerInvoke =
    std::function<
        StorageUserProjectionReconcileResult()
    >;

struct StorageUserProjectionReconcileWorkerConfig {
    StorageUserProjectionReconcileWorkerInvoke
        reconcile_once;

    std::chrono::milliseconds interval{
        std::chrono::seconds(30)
    };
};

class StorageUserProjectionReconcileWorker {
public:
    explicit StorageUserProjectionReconcileWorker(
        StorageUserProjectionReconcileWorkerConfig config
    );

    ~StorageUserProjectionReconcileWorker();

    StorageUserProjectionReconcileWorker(
        const StorageUserProjectionReconcileWorker&
    ) = delete;

    StorageUserProjectionReconcileWorker& operator=(
        const StorageUserProjectionReconcileWorker&
    ) = delete;

    bool start(
        std::string* error
    );

    // Request an immediate reconciliation round.
    //
    // A generation counter prevents a wake arriving during reconciliation
    // from being lost before the worker enters its timed wait.
    bool wake();

    void stop_and_join();

    bool running() const;

private:
    bool config_valid(
        std::string* error
    ) const;

    void run();

    StorageUserProjectionReconcileWorkerConfig
        config_;

    mutable std::mutex state_mutex_;
    std::condition_variable wake_condition_;
    std::thread worker_thread_;

    bool stop_requested_ = false;
    bool running_ = false;

    std::uint64_t wake_generation_ = 0;
};

} // namespace pqnas::cluster
