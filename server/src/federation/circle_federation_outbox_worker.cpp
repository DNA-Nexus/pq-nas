#include "federation/circle_federation_outbox_worker.h"

#include "federation/circle_federation_outbox.h"
#include "federation/pqnas_nodus_client.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace pqnas::federation {
namespace {

std::once_flag g_worker_once;
std::atomic<bool> g_worker_started{false};

bool env_enabled(const char* name) {
    const char* raw = std::getenv(name);
    if (!raw) return false;

    const std::string v(raw);
    return v == "1" || v == "true" || v == "TRUE" || v == "yes" || v == "YES";
}

int env_int(const char* name, int fallback, int lo, int hi) {
    const char* raw = std::getenv(name);
    if (!raw || !raw[0]) return fallback;

    try {
        return std::clamp(std::stoi(raw), lo, hi);
    } catch (...) {
        return fallback;
    }
}

std::string env_string(const char* name, const std::string& fallback) {
    const char* raw = std::getenv(name);
    if (!raw || !raw[0]) return fallback;
    return std::string(raw);
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::vector<NodusSeed> select_worker_seeds(const std::string& selector_raw) {
    const auto seeds = default_nodus_seeds();
    if (seeds.empty()) return {};

    if (selector_raw.empty()) {
        return {seeds.front()};
    }

    const std::string selector = lower_copy(selector_raw);
    if (selector == "all") return seeds;

    std::vector<NodusSeed> out;
    for (const auto& seed : seeds) {
        if (lower_copy(seed.name) == selector || lower_copy(seed.host) == selector) {
            out.push_back(seed);
        }
    }

    if (out.empty()) {
        std::cerr << "[CircleFederationWorker] unknown seed selector '"
                  << selector_raw << "', falling back to "
                  << seeds.front().name << "\n";
        out.push_back(seeds.front());
    }

    return out;
}

NodusClientConfig make_worker_nodus_config() {
    NodusClientConfig config;

    if (const char* cli = std::getenv("PQNAS_NODUS_CLI")) {
        if (cli[0]) config.nodus_cli_path = cli;
    }

    config.identity_dir = "/srv/pqnas/config/nodus/research_identity";
    if (const char* identity_dir = std::getenv("PQNAS_NODUS_IDENTITY_DIR")) {
        if (identity_dir[0]) config.identity_dir = identity_dir;
    }

    config.timeout_seconds = env_int("PQNAS_NODUS_TIMEOUT_SECONDS", 8, 1, 60);

    return config;
}

std::string clamp_error(std::string s) {
    constexpr std::size_t kMax = 2048;
    if (s.size() > kMax) {
        s.resize(kMax);
        s += "...[truncated]";
    }
    return s;
}

int retry_delay_seconds(int attempts) {
    int delay = 30;
    for (int i = 1; i < attempts && delay < 3600; ++i) {
        delay *= 2;
    }
    return std::clamp(delay, 30, 3600);
}

bool publish_one_event_to_seeds(
    const CircleFederationOutboxEvent& ev,
    const NodusClientConfig& config,
    const std::vector<NodusSeed>& seeds,
    std::string* error_out) {
    bool all_ok = true;
    std::ostringstream errors;

    for (const auto& seed : seeds) {
        try {
            const auto event_put = nodus_cli_put(config, seed, ev.event_key, ev.event_json);
            const auto head_put = nodus_cli_put(config, seed, ev.head_key, ev.event_id);

            if (event_put.exit_code != 0 || head_put.exit_code != 0) {
                all_ok = false;
                errors << seed.name << " event_exit=" << event_put.exit_code
                       << " head_exit=" << head_put.exit_code << "\n"
                       << event_put.output << "\n"
                       << head_put.output << "\n";
            }
        } catch (const std::exception& e) {
            all_ok = false;
            errors << seed.name << " exception: " << e.what() << "\n";
        }
    }

    if (!all_ok && error_out) {
        *error_out = clamp_error(errors.str());
    }

    return all_ok;
}

void worker_loop() {
    const int interval_seconds =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_INTERVAL_SECONDS", 10, 1, 3600);
    const int batch_limit =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_BATCH", 5, 1, 50);
    const int lease_seconds =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_LEASE_SECONDS", 300, 10, 3600);
    const int max_attempts =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_MAX_ATTEMPTS", 5, 1, 20);
    const std::string seed_selector =
        env_string("PQNAS_CIRCLE_FEDERATION_WORKER_SEED", "EU-1");

    const auto config = make_worker_nodus_config();
    const auto seeds = select_worker_seeds(seed_selector);

    std::cerr << "[CircleFederationWorker] started interval=" << interval_seconds
              << "s batch=" << batch_limit
              << " lease=" << lease_seconds
              << "s max_attempts=" << max_attempts
              << " seeds=" << seed_selector
              << "\n";

    while (true) {
        try {
            std::string err;
            const int recovered = recover_stale_circle_federation_outbox_leases(&err);
            if (!err.empty()) {
                std::cerr << "[CircleFederationWorker] lease recovery failed: "
                          << err << "\n";
            } else if (recovered > 0) {
                std::cerr << "[CircleFederationWorker] recovered stale leases: "
                          << recovered << "\n";
            }

            err.clear();
            const auto rows = claim_circle_federation_outbox_pending(
                batch_limit,
                lease_seconds,
                &err);

            if (!err.empty()) {
                std::cerr << "[CircleFederationWorker] claim failed: " << err << "\n";
            }

            for (const auto& row : rows) {
                std::string publish_err;
                std::string mark_err;

                if (publish_one_event_to_seeds(row, config, seeds, &publish_err)) {
                    if (!mark_circle_federation_outbox_done(row.id, &mark_err)) {
                        std::cerr << "[CircleFederationWorker] mark done failed id="
                                  << row.id << ": " << mark_err << "\n";
                    } else {
                        std::cerr << "[CircleFederationWorker] published id="
                                  << row.id << " event_id=" << row.event_id << "\n";
                    }
                } else if (row.attempts >= max_attempts) {
                    if (!mark_circle_federation_outbox_failed(row.id, publish_err, &mark_err)) {
                        std::cerr << "[CircleFederationWorker] mark failed failed id="
                                  << row.id << ": " << mark_err << "\n";
                    } else {
                        std::cerr << "[CircleFederationWorker] failed id="
                                  << row.id << " event_id=" << row.event_id << "\n";
                    }
                } else {
                    const int delay = retry_delay_seconds(row.attempts);
                    if (!mark_circle_federation_outbox_retry(row.id, publish_err, delay, &mark_err)) {
                        std::cerr << "[CircleFederationWorker] mark retry failed id="
                                  << row.id << ": " << mark_err << "\n";
                    } else {
                        std::cerr << "[CircleFederationWorker] retry id="
                                  << row.id << " in " << delay << "s\n";
                    }
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[CircleFederationWorker] exception: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[CircleFederationWorker] unknown exception\n";
        }

        std::this_thread::sleep_for(std::chrono::seconds(interval_seconds));
    }
}

} // namespace

void start_circle_federation_outbox_worker_once() {
    std::call_once(g_worker_once, [] {
        if (!env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER")) {
            std::cerr << "[CircleFederationWorker] disabled "
                      << "(set PQNAS_CIRCLE_FEDERATION_WORKER=1 to enable)\n";
            return;
        }

        g_worker_started = true;

        std::thread t(worker_loop);
        t.detach();
    });
}

} // namespace pqnas::federation
