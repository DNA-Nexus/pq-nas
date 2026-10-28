#include "cluster/storage_user_projection_reconcile_worker.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace {

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

} // namespace

int main() {
    using namespace std::chrono_literals;

    {
        pqnas::cluster::
            StorageUserProjectionReconcileWorkerConfig
                invalid_config;

        pqnas::cluster::
            StorageUserProjectionReconcileWorker
                invalid_worker(
                    invalid_config
                );

        std::string error;

        expect(
            !invalid_worker.start(
                &error
            ),
            "missing callback prevents worker startup"
        );
    }

    {
        pqnas::cluster::
            StorageUserProjectionReconcileWorkerConfig
                invalid_interval_config;

        invalid_interval_config.reconcile_once =
            []() {
                return pqnas::cluster::
                    StorageUserProjectionReconcileResult{};
            };

        invalid_interval_config.interval =
            0ms;

        pqnas::cluster::
            StorageUserProjectionReconcileWorker
                invalid_interval_worker(
                    invalid_interval_config
                );

        std::string error;

        expect(
            !invalid_interval_worker.start(
                &error
            ),
            "non-positive interval prevents worker startup"
        );
    }

    std::mutex callback_mutex;
    std::condition_variable callback_condition;

    std::size_t calls = 0;
    bool throw_on_second_call = true;

    pqnas::cluster::
        StorageUserProjectionReconcileWorkerConfig
            config;

    // Long interval keeps this test deterministic: extra rounds should come
    // from explicit wake(), not from the timer.
    config.interval =
        std::chrono::hours(24);

    config.reconcile_once =
        [&]() {
            std::size_t current_call = 0;

            {
                std::lock_guard<std::mutex> lock(
                    callback_mutex
                );

                ++calls;
                current_call = calls;
            }

            callback_condition.notify_all();

            if (current_call == 2 &&
                throw_on_second_call) {
                throw std::runtime_error(
                    "test reconciliation failure"
                );
            }

            pqnas::cluster::
                StorageUserProjectionReconcileResult
                    result;

            result.ok = true;

            return result;
        };

    pqnas::cluster::
        StorageUserProjectionReconcileWorker
            worker(config);

    std::string error;

    expect(
        worker.start(
            &error
        ),
        "worker starts"
    );

    {
        std::unique_lock<std::mutex> lock(
            callback_mutex
        );

        expect(
            callback_condition.wait_for(
                lock,
                2s,
                [&]() {
                    return calls >= 1;
                }
            ),
            "startup triggers immediate reconciliation"
        );
    }

    expect(
        worker.running(),
        "worker reports running after startup"
    );

    expect(
        !worker.start(
            &error
        ),
        "worker cannot be started twice"
    );

    expect(
        worker.wake(),
        "running worker accepts explicit wake"
    );

    {
        std::unique_lock<std::mutex> lock(
            callback_mutex
        );

        expect(
            callback_condition.wait_for(
                lock,
                2s,
                [&]() {
                    return calls >= 2;
                }
            ),
            "explicit wake triggers reconciliation"
        );
    }

    // The second callback deliberately threw. A third wake must still work,
    // proving that one reconciliation exception does not kill the worker.
    throw_on_second_call = false;

    expect(
        worker.wake(),
        "worker survives reconciliation exception"
    );

    {
        std::unique_lock<std::mutex> lock(
            callback_mutex
        );

        expect(
            callback_condition.wait_for(
                lock,
                2s,
                [&]() {
                    return calls >= 3;
                }
            ),
            "worker reconciles again after callback exception"
        );
    }

    worker.stop_and_join();

    expect(
        !worker.running(),
        "worker stops and joins"
    );

    expect(
        !worker.wake(),
        "stopped worker rejects wake"
    );

    // stop_and_join() is deliberately idempotent for shutdown paths and the
    // destructor.
    worker.stop_and_join();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection reconciliation worker test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection reconciliation worker tests passed\n";

    return 0;
}
