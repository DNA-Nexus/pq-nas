#include "federation/circle_federation_outbox_worker.h"

#include "federation/circle_federation_outbox.h"
#include "federation/circle_federation_inbox.h"
#include "federation/circle_federation_remote_feed.h"
#include "federation/circle_federation_event.h"
#include "federation/pqnas_nodus_client.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <cstdint>
#include <cctype>
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

using json = nlohmann::json;

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


std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string read_first_line_trimmed(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f) return "";

    std::string line;
    std::getline(f, line);
    return trim_copy(line);
}

std::string nodus_identity_fingerprint_from_dir(const std::string& dir) {
    if (dir.empty()) return "";
    return read_first_line_trimmed(std::filesystem::path(dir) / "nodus.fp");
}

std::string extract_nodus_value(const std::string& output) {
    const std::string marker = "Value: ";
    const auto pos = output.find(marker);
    if (pos == std::string::npos) return "";

    const auto start = pos + marker.size();
    const auto end = output.find('\n', start);

    if (end == std::string::npos) {
        return trim_copy(output.substr(start));
    }

    return trim_copy(output.substr(start, end - start));
}

bool parse_remote_feed_fields_from_event(
    const json& event,
    std::string* target_type,
    std::int64_t* post_id,
    std::int64_t* reply_id,
    std::string* actor_fp,
    std::string* reaction) {
    if (target_type) *target_type = "";
    if (post_id) *post_id = 0;
    if (reply_id) *reply_id = 0;
    if (actor_fp) *actor_fp = "";
    if (reaction) *reaction = "";

    if (!event.contains("payload") || !event["payload"].is_object()) {
        return true;
    }

    const auto& payload = event["payload"];

    if (target_type && payload.contains("target_type") && payload["target_type"].is_string()) {
        *target_type = payload["target_type"].get<std::string>();
    }

    if (post_id && payload.contains("post_id") && payload["post_id"].is_number_integer()) {
        *post_id = payload["post_id"].get<std::int64_t>();
    }

    if (reply_id && payload.contains("reply_id") && payload["reply_id"].is_number_integer()) {
        *reply_id = payload["reply_id"].get<std::int64_t>();
    }

    if (actor_fp) {
        if (payload.contains("actor_fp") && payload["actor_fp"].is_string()) {
            *actor_fp = payload["actor_fp"].get<std::string>();
        } else if (payload.contains("owner_fp") && payload["owner_fp"].is_string()) {
            *actor_fp = payload["owner_fp"].get<std::string>();
        }
    }

    if (reaction && payload.contains("reaction") && payload["reaction"].is_string()) {
        *reaction = payload["reaction"].get<std::string>();
    }

    return true;
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

    const std::string production_identity_dir = "/srv/pqnas/config/nodus/identity";
    const std::string legacy_identity_dir = "/srv/pqnas/config/nodus/research_identity";

    config.identity_dir = production_identity_dir;

    if (const char* identity_dir = std::getenv("PQNAS_NODUS_IDENTITY_DIR")) {
        if (identity_dir[0]) {
            config.identity_dir = identity_dir;
        }
    } else if (nodus_identity_fingerprint_from_dir(production_identity_dir).empty() &&
               !nodus_identity_fingerprint_from_dir(legacy_identity_dir).empty()) {
        config.identity_dir = legacy_identity_dir;
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

int recent_slot_for_outbox_id(std::int64_t id, int slots) {
    if (slots <= 0) return 0;
    if (id < 0) id = -id;
    return static_cast<int>(id % slots);
}

bool publish_one_event_to_seeds(
    const CircleFederationOutboxEvent& ev,
    const NodusClientConfig& config,
    const std::vector<NodusSeed>& seeds,
    int recent_slots,
    std::string* error_out) {
    bool all_ok = true;
    std::ostringstream errors;

    for (const auto& seed : seeds) {
        try {
            const auto event_put = nodus_cli_put(config, seed, ev.event_key, ev.event_json);

            if (event_put.exit_code != 0) {
                all_ok = false;
                errors << seed.name << " event_exit=" << event_put.exit_code
                       << " head_exit=skipped recent_exit=skipped\n"
                       << event_put.output << "\n";
                continue;
            }

            const auto head_put = nodus_cli_put(config, seed, ev.head_key, ev.event_id);

            const int recent_slot = recent_slot_for_outbox_id(ev.id, recent_slots);
            const std::string recent_key = circle_recent_key(ev.circle_id, recent_slot);
            const auto recent_put = nodus_cli_put(config, seed, recent_key, ev.event_id);

            if (head_put.exit_code != 0 || recent_put.exit_code != 0) {
                all_ok = false;
                errors << seed.name << " event_exit=" << event_put.exit_code
                       << " head_exit=" << head_put.exit_code
                       << " recent_exit=" << recent_put.exit_code << "\n"
                       << event_put.output << "\n"
                       << head_put.output << "\n"
                       << recent_put.output << "\n";
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


bool worker_pull_latest_remote_head(
    const std::string& circle_id,
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& local_nodus_fp) {
    const std::string head_key = circle_head_key(circle_id);

    NodusCommandResult head_get;
    try {
        head_get = nodus_cli_get(config, seed, head_key);
    } catch (const std::exception& e) {
        std::cerr << "[CircleFederationWorker] inbound head get exception seed="
                  << seed.name << ": " << e.what() << "\n";
        return false;
    }

    const std::string event_id = extract_nodus_value(head_get.output);
    if (head_get.exit_code != 0 || event_id.empty()) {
        return false;
    }

    const std::string event_key = circle_event_key(circle_id, event_id);

    NodusCommandResult event_get;
    try {
        event_get = nodus_cli_get(config, seed, event_key);
    } catch (const std::exception& e) {
        std::cerr << "[CircleFederationWorker] inbound event get exception seed="
                  << seed.name << " event_id=" << event_id
                  << ": " << e.what() << "\n";
        return false;
    }

    const std::string event_json_raw = extract_nodus_value(event_get.output);
    if (event_get.exit_code != 0 || event_json_raw.empty()) {
        std::cerr << "[CircleFederationWorker] inbound event not found seed="
                  << seed.name << " event_id=" << event_id << "\n";
        return false;
    }

    json event;
    try {
        event = json::parse(event_json_raw);
    } catch (...) {
        std::cerr << "[CircleFederationWorker] inbound invalid event JSON event_id="
                  << event_id << "\n";
        return false;
    }

    const std::string parsed_circle_id = event.value("circle_id", "");
    const std::string parsed_event_id = event.value("event_id", "");
    const std::string event_type = event.value("type", "");
    const std::string origin_nas = event.value("origin_nas", "");
    const std::int64_t created_epoch = event.value("created_epoch", 0LL);

    if (parsed_circle_id != circle_id ||
        parsed_event_id != event_id ||
        event_type.empty() ||
        origin_nas.empty()) {
        std::cerr << "[CircleFederationWorker] inbound invalid event fields event_id="
                  << event_id << "\n";
        return false;
    }

    if (!local_nodus_fp.empty() && origin_nas == local_nodus_fp) {
        return false;
    }

    std::string store_err;
    if (!store_circle_federation_inbox_event(
            parsed_circle_id,
            parsed_event_id,
            event_type,
            origin_nas,
            created_epoch,
            event_key,
            event_json_raw,
            &store_err)) {
        std::cerr << "[CircleFederationWorker] inbound store failed event_id="
                  << event_id << ": " << store_err << "\n";
        return false;
    }

    return true;
}


void worker_pull_recent_remote_events(
    const std::string& circle_id,
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& local_nodus_fp,
    int recent_slots) {
    for (int slot = 0; slot < recent_slots; ++slot) {
        const std::string recent_key = circle_recent_key(circle_id, slot);

        NodusCommandResult recent_get;
        try {
            recent_get = nodus_cli_get(config, seed, recent_key);
        } catch (const std::exception& e) {
            std::cerr << "[CircleFederationWorker] inbound recent get exception seed="
                      << seed.name << " slot=" << slot << ": " << e.what() << "\n";
            continue;
        }

        const std::string event_id = extract_nodus_value(recent_get.output);
        if (recent_get.exit_code != 0 || event_id.empty()) {
            continue;
        }

        const std::string event_key = circle_event_key(circle_id, event_id);

        NodusCommandResult event_get;
        try {
            event_get = nodus_cli_get(config, seed, event_key);
        } catch (const std::exception& e) {
            std::cerr << "[CircleFederationWorker] inbound recent event get exception seed="
                      << seed.name << " slot=" << slot << " event_id=" << event_id
                      << ": " << e.what() << "\n";
            continue;
        }

        const std::string event_json_raw = extract_nodus_value(event_get.output);
        if (event_get.exit_code != 0 || event_json_raw.empty()) {
            continue;
        }

        json event;
        try {
            event = json::parse(event_json_raw);
        } catch (...) {
            std::cerr << "[CircleFederationWorker] inbound recent invalid JSON event_id="
                      << event_id << "\n";
            continue;
        }

        const std::string parsed_circle_id = event.value("circle_id", "");
        const std::string parsed_event_id = event.value("event_id", "");
        const std::string event_type = event.value("type", "");
        const std::string origin_nas = event.value("origin_nas", "");
        const std::int64_t created_epoch = event.value("created_epoch", 0LL);

        if (parsed_circle_id != circle_id ||
            parsed_event_id != event_id ||
            event_type.empty() ||
            origin_nas.empty()) {
            continue;
        }

        if (!local_nodus_fp.empty() && origin_nas == local_nodus_fp) {
            continue;
        }

        std::string store_err;
        if (!store_circle_federation_inbox_event(
                parsed_circle_id,
                parsed_event_id,
                event_type,
                origin_nas,
                created_epoch,
                event_key,
                event_json_raw,
                &store_err)) {
            std::cerr << "[CircleFederationWorker] inbound recent store failed event_id="
                      << event_id << ": " << store_err << "\n";
            continue;
        }
    }
}


void worker_apply_pending_inbox(
    const NodusClientConfig& config,
    int batch_limit) {
    std::string err;
    const auto rows = list_circle_federation_inbox(batch_limit, &err);
    if (!err.empty()) {
        std::cerr << "[CircleFederationWorker] inbound inbox list failed: "
                  << err << "\n";
        return;
    }

    const std::string local_nodus_fp =
        nodus_identity_fingerprint_from_dir(config.identity_dir);

    for (const auto& row : rows) {
        if (row.status != "pending") continue;

        std::string mark_err;

        if (!local_nodus_fp.empty() && row.origin_nas == local_nodus_fp) {
            if (!mark_circle_federation_inbox_ignored(
                    row.id,
                    "ignored_local_origin",
                    &mark_err)) {
                std::cerr << "[CircleFederationWorker] inbound mark ignored failed id="
                          << row.id << ": " << mark_err << "\n";
            }
            continue;
        }

        json event;
        try {
            event = json::parse(row.event_json);
        } catch (...) {
            if (!mark_circle_federation_inbox_failed(
                    row.id,
                    "invalid_event_json",
                    &mark_err)) {
                std::cerr << "[CircleFederationWorker] inbound mark invalid JSON failed id="
                          << row.id << ": " << mark_err << "\n";
            }
            continue;
        }

        std::string target_type;
        std::int64_t post_id = 0;
        std::int64_t reply_id = 0;
        std::string event_actor_fp;
        std::string reaction;

        parse_remote_feed_fields_from_event(
            event,
            &target_type,
            &post_id,
            &reply_id,
            &event_actor_fp,
            &reaction);

        std::string store_err;
        if (!store_circle_federation_remote_feed_event(
                row.circle_id,
                row.event_id,
                row.event_type,
                row.origin_nas,
                row.created_epoch,
                target_type,
                post_id,
                reply_id,
                event_actor_fp,
                reaction,
                row.event_json,
                &store_err)) {
            if (!mark_circle_federation_inbox_failed(
                    row.id,
                    store_err,
                    &mark_err)) {
                std::cerr << "[CircleFederationWorker] inbound mark store failed id="
                          << row.id << ": " << mark_err << "\n";
            }
            continue;
        }

        if (!mark_circle_federation_inbox_applied(row.id, &mark_err)) {
            std::cerr << "[CircleFederationWorker] inbound mark applied failed id="
                      << row.id << ": " << mark_err << "\n";
            continue;
        }

        std::cerr << "[CircleFederationWorker] inbound applied event_id="
                  << row.event_id << " origin_nas=" << row.origin_nas << "\n";
    }
}

void worker_pull_and_apply_inbound(
    const std::string& circle_id,
    const NodusClientConfig& config,
    const std::vector<NodusSeed>& seeds,
    int batch_limit,
    int recent_slots) {
    const std::string local_nodus_fp =
        nodus_identity_fingerprint_from_dir(config.identity_dir);

    // First clear anything already discovered in a previous loop.
    worker_apply_pending_inbox(config, batch_limit);

    for (const auto& seed : seeds) {
        worker_pull_latest_remote_head(circle_id, config, seed, local_nodus_fp);
        worker_pull_recent_remote_events(circle_id, config, seed, local_nodus_fp, recent_slots);
    }

    // Then apply anything discovered during this loop immediately.
    worker_apply_pending_inbox(config, batch_limit);
}


void worker_loop() {
    const int interval_seconds =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_INTERVAL_SECONDS", 10, 1, 3600);
    const int batch_limit =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_BATCH", 5, 1, 50);
    const int inbox_batch_limit =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_INBOX_BATCH", 100, 1, 500);
    const int lease_seconds =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_LEASE_SECONDS", 300, 10, 3600);
    const int max_attempts =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_MAX_ATTEMPTS", 5, 1, 20);
    const int recent_slots =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_RECENT_SLOTS", 64, 4, 512);
    const std::string seed_selector =
        env_string("PQNAS_CIRCLE_FEDERATION_WORKER_SEED", "EU-1");
    const std::string inbound_circle_id =
        env_string("PQNAS_CIRCLE_FEDERATION_WORKER_CIRCLE_ID", "local-public-feed");

    const auto config = make_worker_nodus_config();
    const auto seeds = select_worker_seeds(seed_selector);

    std::cerr << "[CircleFederationWorker] started interval=" << interval_seconds
              << "s batch=" << batch_limit
              << " inbox_batch=" << inbox_batch_limit
              << " lease=" << lease_seconds
              << "s max_attempts=" << max_attempts
              << " recent_slots=" << recent_slots
              << " seeds=" << seed_selector
              << " inbound_circle=" << inbound_circle_id
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

                if (publish_one_event_to_seeds(row, config, seeds, recent_slots, &publish_err)) {
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

            worker_pull_and_apply_inbound(
                inbound_circle_id,
                config,
                seeds,
                inbox_batch_limit,
                recent_slots);
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
