#include "cluster/storage_user_projection_reconcile_worker.h"

#include <utility>

namespace pqnas::cluster {
namespace {

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

} // namespace

StorageUserProjectionReconcileWorker::
StorageUserProjectionReconcileWorker(
    StorageUserProjectionReconcileWorkerConfig config
)
    : config_(std::move(config)) {}

StorageUserProjectionReconcileWorker::
~StorageUserProjectionReconcileWorker() {
    stop_and_join();
}

bool StorageUserProjectionReconcileWorker::
config_valid(
    std::string* error
) const {
    if (!config_.reconcile_once) {
        set_error(
            error,
            "projection reconciliation callback is missing"
        );

        return false;
    }

    if (config_.interval.count() <= 0) {
        set_error(
            error,
            "projection reconciliation interval must be positive"
        );

        return false;
    }

    return true;
}

bool StorageUserProjectionReconcileWorker::start(
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

    if (running_ ||
        worker_thread_.joinable()) {
        set_error(
            error,
            "projection reconciliation worker is already running"
        );

        return false;
    }

    stop_requested_ = false;
    running_ = true;

    try {
        worker_thread_ =
            std::thread(
                [this]() {
                    run();
                }
            );
    } catch (...) {
        running_ = false;
        stop_requested_ = true;

        set_error(
            error,
            "failed to start projection reconciliation worker"
        );

        return false;
    }

    return true;
}

bool StorageUserProjectionReconcileWorker::wake() {
    {
        std::lock_guard<std::mutex> lock(
            state_mutex_
        );

        if (!running_ ||
            stop_requested_) {
            return false;
        }

        ++wake_generation_;
    }

    wake_condition_.notify_one();

    return true;
}

void StorageUserProjectionReconcileWorker::
stop_and_join() {
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

bool StorageUserProjectionReconcileWorker::
running() const {
    std::lock_guard<std::mutex> lock(
        state_mutex_
    );

    return running_;
}

void StorageUserProjectionReconcileWorker::run() {
    while (true) {
        std::uint64_t observed_generation = 0;

        {
            std::lock_guard<std::mutex> lock(
                state_mutex_
            );

            if (stop_requested_) {
                break;
            }

            observed_generation =
                wake_generation_;
        }

        try {
            // All authorization-transition policy remains inside the
            // deterministic reconciler. The worker only schedules execution.
            (void)config_.reconcile_once();
        } catch (...) {
            // Security/availability: a single reconciliation failure must not
            // permanently kill future revoke/synchronization retries.
        }

        std::unique_lock<std::mutex> lock(
            state_mutex_
        );

        if (stop_requested_) {
            break;
        }

        // A wake that arrived while reconciliation was running must trigger
        // another round immediately instead of being lost until the interval.
        if (wake_generation_ !=
            observed_generation) {
            continue;
        }

        (void)wake_condition_.wait_for(
            lock,
            config_.interval,
            [this, observed_generation]() {
                return
                    stop_requested_ ||
                    wake_generation_ !=
                        observed_generation;
            }
        );

        if (stop_requested_) {
            break;
        }
    }

    std::lock_guard<std::mutex> lock(
        state_mutex_
    );

    running_ = false;
}

} // namespace pqnas::cluster
