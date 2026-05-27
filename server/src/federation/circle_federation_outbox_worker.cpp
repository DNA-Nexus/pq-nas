#include "federation/circle_federation_outbox_worker.h"

#include "federation/circle_federation_outbox.h"
#include "federation/circle_federation_inbox.h"
#include "federation/circle_federation_limits.h"
#include "federation/circle_federation_remote_feed.h"
#include "federation/circle_federation_signing.h"
#include "federation/circle_federation_event.h"
#include "federation/pqnas_nodus_client.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

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

bool event_json_too_large(
    const std::string& event_json_raw,
    const std::string& event_id,
    const char* source_label) {
    if (event_json_raw.size() <= kMaxCircleFederationEventJsonBytes) {
        return false;
    }

    std::cerr << "[CircleFederationWorker] inbound " << source_label
              << " event JSON too large event_id=" << event_id
              << " bytes=" << event_json_raw.size()
              << " max=" << kMaxCircleFederationEventJsonBytes << "\n";
    return true;
}

bool is_safe_circle_federation_event_id(const std::string& event_id) {
    if (event_id.size() < 3 || event_id.size() > 160) {
        return false;
    }

    for (unsigned char c : event_id) {
        if (std::isalnum(c) ||
            c == '_' ||
            c == '-' ||
            c == '.' ||
            c == ':') {
            continue;
        }

        return false;
    }

    return true;
}

void log_rejected_remote_event_id(
    const char* source_label,
    const NodusSeed& seed,
    const std::string& event_id) {
    std::cerr << "[CircleFederationWorker] inbound " << source_label
              << " rejected unsafe event_id seed=" << seed.name
              << " bytes=" << event_id.size() << "\n";
}

std::string canonical_federation_event_without_signature(json event) {
    if (event.is_object()) {
        event.erase("origin_sig");
    }

    return event.dump(
        -1,
        ' ',
        false,
        nlohmann::json::error_handler_t::strict);
}

bool verify_federation_event_signature(
    const json& event,
    const std::string& event_id,
    const char* source_label) {
    if (!event.is_object()) {
        return false;
    }

    const std::string origin_nas =
        event.value("origin_nas", "");
    const std::string origin_pubkey =
        event.value("origin_pubkey", "");
    const std::string origin_sig =
        event.value("origin_sig", "");
    const std::string origin_sig_alg =
        event.value("origin_sig_alg", "");

    if (origin_nas.empty() ||
        origin_pubkey.empty() ||
        origin_sig.empty() ||
        origin_sig_alg != "Ed25519") {
        std::cerr << "[CircleFederationWorker] inbound " << source_label
                  << " unsigned event rejected event_id=" << event_id << "\n";
        return false;
    }

    const std::string signing_fp =
        circle_federation_signing_public_key_fingerprint(origin_pubkey);

    if (signing_fp.empty() || signing_fp != origin_nas) {
        std::cerr << "[CircleFederationWorker] inbound " << source_label
                  << " signing key fingerprint mismatch event_id=" << event_id
                  << "\n";
        return false;
    }

    const std::string canonical =
        canonical_federation_event_without_signature(event);

    std::string err;
    if (!verify_circle_federation_canonical_json(
            origin_pubkey,
            canonical,
            origin_sig,
            &err)) {
        std::cerr << "[CircleFederationWorker] inbound " << source_label
                  << " signature verify failed event_id=" << event_id
                  << ": " << err << "\n";
        return false;
    }

    return true;
}

std::string local_federation_origin_fingerprint(
    const NodusClientConfig& config) {
    CircleFederationSigningIdentity identity;
    std::string err;

    if (ensure_circle_federation_signing_identity(
            config.identity_dir,
            &identity,
            &err)) {
        return identity.public_key_fingerprint;
    }

    // Compatibility fallback for older unsigned research events.
    return nodus_identity_fingerprint_from_dir(config.identity_dir);
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


// FEDERATED_REACTION_WORKER_REDUCER_PATCH_V1

static constexpr const char* kCircleStackDbPathForWorkerReducer =
    "/srv/pqnas/circlestack.db";

bool worker_supported_reaction_for_local_reduce(const std::string& reaction) {
    return reaction == "👍" ||
           reaction == "❤️" ||
           reaction == "😂" ||
           reaction == "😮" ||
           reaction == "👏" ||
           reaction == "🔥";
}

std::string worker_short_fp_for_local_reduce(const std::string& fp) {
    return fp.size() >= 8 ? fp.substr(0, 8) : fp;
}

long long worker_post_id_from_federation_event_id(const std::string& event_id) {
    const std::string prefix = "post_";
    if (event_id.rfind(prefix, 0) != 0) return 0;

    const auto pos = event_id.rfind('_');
    if (pos == std::string::npos || pos + 1 >= event_id.size()) return 0;

    try {
        const long long id = std::stoll(event_id.substr(pos + 1));
        return id > 0 ? id : 0;
    } catch (...) {
        return 0;
    }
}

std::string worker_expected_post_event_id(
    long long post_id,
    long long created_epoch
) {
    return "post_" + std::to_string(created_epoch) + "_" + std::to_string(post_id);
}

bool worker_ensure_federated_post_reactions_table(sqlite3* db, std::string* err) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS federated_post_reactions ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "local_post_id INTEGER NOT NULL,"
        "target_event_id TEXT NOT NULL,"
        "remote_event_id TEXT NOT NULL,"
        "remote_origin_nas TEXT NOT NULL,"
        "actor_fp TEXT NOT NULL,"
        "actor_display_name TEXT NOT NULL DEFAULT '',"
        "reaction TEXT NOT NULL,"
        "created_epoch INTEGER NOT NULL,"
        "removed_epoch INTEGER NOT NULL DEFAULT 0,"
        "UNIQUE(remote_event_id),"
        "UNIQUE(local_post_id, remote_origin_nas, actor_fp)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_federated_post_reactions_local_post "
        "ON federated_post_reactions(local_post_id, removed_epoch);";

    char* msg = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &msg) != SQLITE_OK) {
        if (err) *err = msg ? msg : sqlite3_errmsg(db);
        if (msg) sqlite3_free(msg);
        return false;
    }

    if (msg) sqlite3_free(msg);
    return true;
}

bool worker_validate_local_post_target(
    sqlite3* db,
    const std::string& target_event_id,
    long long* out_post_id,
    std::string* err
) {
    if (out_post_id) *out_post_id = 0;

    const long long post_id =
        worker_post_id_from_federation_event_id(target_event_id);

    if (post_id <= 0) {
        if (err) *err = "invalid_target_event_id";
        return false;
    }

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT created_epoch, visibility FROM posts WHERE id = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    sqlite3_bind_int64(st, 1, (sqlite3_int64)post_id);

    long long created_epoch = 0;
    std::string visibility;

    if (sqlite3_step(st) == SQLITE_ROW) {
        created_epoch = sqlite3_column_int64(st, 0);
        const char* vis_raw =
            reinterpret_cast<const char*>(sqlite3_column_text(st, 1));
        visibility = vis_raw ? vis_raw : "";
    }

    sqlite3_finalize(st);

    if (created_epoch <= 0) {
        if (err) *err = "target_local_post_not_found";
        return false;
    }

    if (visibility != "public") {
        if (err) *err = "target_local_post_not_public";
        return false;
    }

    const std::string expected =
        worker_expected_post_event_id(post_id, created_epoch);

    if (target_event_id != expected) {
        if (err) *err = "target_event_id_mismatch";
        return false;
    }

    if (out_post_id) *out_post_id = post_id;
    return true;
}

bool worker_apply_federated_post_reaction_created(
    sqlite3* db,
    long long local_post_id,
    const std::string& target_event_id,
    const std::string& remote_event_id,
    const std::string& remote_origin_nas,
    const std::string& actor_fp,
    const std::string& actor_display_name_raw,
    const std::string& reaction,
    long long created_epoch,
    std::string* err
) {
    std::string actor_display_name = trim_copy(actor_display_name_raw);
    if (actor_display_name.size() > 120) {
        actor_display_name.resize(120);
    }
    if (actor_display_name.empty()) {
        actor_display_name =
            worker_short_fp_for_local_reduce(actor_fp) + " · remote NAS";
    }

    char* msg = nullptr;
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", nullptr, nullptr, &msg) != SQLITE_OK) {
        if (err) *err = msg ? msg : sqlite3_errmsg(db);
        if (msg) sqlite3_free(msg);
        return false;
    }
    if (msg) {
        sqlite3_free(msg);
        msg = nullptr;
    }

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "DELETE FROM federated_post_reactions "
            "WHERE local_post_id = ? AND remote_origin_nas = ? AND actor_fp = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_int64(st, 1, (sqlite3_int64)local_post_id);
    sqlite3_bind_text(st, 2, remote_origin_nas.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_finalize(st);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_finalize(st);
    st = nullptr;

    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO federated_post_reactions "
            "(local_post_id, target_event_id, remote_event_id, remote_origin_nas, "
            " actor_fp, actor_display_name, reaction, created_epoch, removed_epoch) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0)",
            -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_bind_int64(st, 1, (sqlite3_int64)local_post_id);
    sqlite3_bind_text(st, 2, target_event_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, remote_event_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, remote_origin_nas.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, actor_display_name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, reaction.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 8, (sqlite3_int64)created_epoch);

    if (sqlite3_step(st) != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_finalize(st);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    sqlite3_finalize(st);

    if (sqlite3_exec(db, "COMMIT", nullptr, nullptr, &msg) != SQLITE_OK) {
        if (err) *err = msg ? msg : sqlite3_errmsg(db);
        if (msg) sqlite3_free(msg);
        sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
        return false;
    }

    if (msg) sqlite3_free(msg);
    return true;
}

bool worker_apply_federated_post_reaction_removed(
    sqlite3* db,
    long long local_post_id,
    const std::string& target_event_id,
    const std::string& remote_event_id,
    const std::string& remote_origin_nas,
    const std::string& actor_fp,
    long long removed_epoch,
    std::string* err
) {
    sqlite3_stmt* st = nullptr;

    if (sqlite3_prepare_v2(db,
            "UPDATE federated_post_reactions "
            "SET removed_epoch = ?, remote_event_id = ? "
            "WHERE local_post_id = ? AND remote_origin_nas = ? AND actor_fp = ?",
            -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    sqlite3_bind_int64(st, 1, (sqlite3_int64)removed_epoch);
    sqlite3_bind_text(st, 2, remote_event_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 3, (sqlite3_int64)local_post_id);
    sqlite3_bind_text(st, 4, remote_origin_nas.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, actor_fp.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(st) != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_finalize(st);
        return false;
    }

    const int changed = sqlite3_changes(db);
    sqlite3_finalize(st);

    if (changed > 0) {
        return true;
    }

    if (sqlite3_prepare_v2(db,
            "INSERT OR IGNORE INTO federated_post_reactions "
            "(local_post_id, target_event_id, remote_event_id, remote_origin_nas, "
            " actor_fp, actor_display_name, reaction, created_epoch, removed_epoch) "
            "VALUES (?, ?, ?, ?, ?, '', '', ?, ?)",
            -1, &st, nullptr) != SQLITE_OK) {
        if (err) *err = sqlite3_errmsg(db);
        return false;
    }

    sqlite3_bind_int64(st, 1, (sqlite3_int64)local_post_id);
    sqlite3_bind_text(st, 2, target_event_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, remote_event_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, remote_origin_nas.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, actor_fp.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, (sqlite3_int64)removed_epoch);
    sqlite3_bind_int64(st, 7, (sqlite3_int64)removed_epoch);

    if (sqlite3_step(st) != SQLITE_DONE) {
        if (err) *err = sqlite3_errmsg(db);
        sqlite3_finalize(st);
        return false;
    }

    sqlite3_finalize(st);
    return true;
}

bool worker_reduce_federated_reaction_to_local_post_if_targeted(
    const CircleFederationInboxEvent& row,
    const json& event,
    const std::string& local_nodus_fp,
    bool* out_reduced,
    std::string* err
) {
    if (out_reduced) *out_reduced = false;
    if (err) *err = "";

    const std::string event_type = event.value("type", "");
    if (event_type != "circle.reaction.created" &&
        event_type != "circle.reaction.removed") {
        return true;
    }

    if (local_nodus_fp.empty()) {
        return true;
    }

    if (!event.contains("payload") || !event["payload"].is_object()) {
        return true;
    }

    const auto& payload = event["payload"];

    const std::string target_type = payload.value("target_type", "");
    if (target_type != "post") {
        return true;
    }

    const std::string target_origin_nas =
        payload.value("target_origin_nas", "");

    if (target_origin_nas.empty() || target_origin_nas != local_nodus_fp) {
        return true;
    }

    if (out_reduced) *out_reduced = true;

    const std::string target_event_id =
        payload.value("target_event_id", "");
    const std::string actor_fp =
        payload.value("actor_fp", "");
    const std::string actor_display_name =
        payload.value("actor_display_name", "");
    const std::string reaction =
        payload.value("reaction", "");

    if (target_event_id.empty() || actor_fp.empty()) {
        if (err) *err = "missing_target_event_or_actor";
        return false;
    }

    if (event_type == "circle.reaction.created" &&
        !worker_supported_reaction_for_local_reduce(reaction)) {
        if (err) *err = "unsupported_reaction";
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(kCircleStackDbPathForWorkerReducer, &db) != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite3_open_failed";
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_busy_timeout(db, 5000);

    bool ok = true;
    std::string local_err;

    if (!worker_ensure_federated_post_reactions_table(db, &local_err)) {
        ok = false;
    }

    long long local_post_id = 0;
    if (ok &&
        !worker_validate_local_post_target(
            db,
            target_event_id,
            &local_post_id,
            &local_err)) {
        ok = false;
    }

    const long long event_epoch =
        event.value("created_epoch", row.created_epoch);

    if (ok && event_type == "circle.reaction.created") {
        ok = worker_apply_federated_post_reaction_created(
            db,
            local_post_id,
            target_event_id,
            row.event_id,
            row.origin_nas,
            actor_fp,
            actor_display_name,
            reaction,
            event_epoch > 0 ? event_epoch : (long long)std::time(nullptr),
            &local_err);
    } else if (ok && event_type == "circle.reaction.removed") {
        ok = worker_apply_federated_post_reaction_removed(
            db,
            local_post_id,
            target_event_id,
            row.event_id,
            row.origin_nas,
            actor_fp,
            event_epoch > 0 ? event_epoch : (long long)std::time(nullptr),
            &local_err);
    }

    sqlite3_close(db);

    if (!ok && err) {
        *err = local_err.empty() ? "federated_reaction_worker_reduce_failed" : local_err;
    }

    return ok;
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

    // Keep worker/admin Nodus subprocess calls short enough that a slow seed
    // cannot stall federation sync for minutes. Operators can still tune this
    // through PQNAS_NODUS_TIMEOUT_SECONDS, but cap it to a sane research limit.
    config.timeout_seconds = env_int("PQNAS_NODUS_TIMEOUT_SECONDS", 5, 1, 30);

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


// ORIGIN_SCOPED_RECENT_INDEX_WORKER_PATCH_V1

bool is_safe_nodus_key_segment_for_worker(const std::string& value) {
    if (value.empty() || value.size() > 180) return false;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.') {
            continue;
        }
        return false;
    }

    return true;
}

std::string circle_origin_recent_index_key_for_worker(
    const std::string& circle_id,
    const std::string& origin_nas
) {
    if (!is_safe_nodus_key_segment_for_worker(circle_id) ||
        !is_safe_nodus_key_segment_for_worker(origin_nas)) {
        return "";
    }

    return "pqnas:circlestack:circle:" + circle_id +
           ":origin:" + origin_nas + ":recent:index";
}

std::string event_origin_nas_from_json_for_worker(const std::string& raw) {
    json ev = json::parse(raw, nullptr, false);
    if (!ev.is_object()) return "";

    if (ev.contains("origin_nas") && ev["origin_nas"].is_string()) {
        return trim_copy(ev["origin_nas"].get<std::string>());
    }

    return "";
}


// KNOWN_REMOTE_ORIGINS_DB_WORKER_PATCH_V1

static constexpr const char* kPeopleContactsDbPathForKnownRemoteOrigins =
    "/srv/pqnas/config/people_contacts.sqlite3";

void append_unique_remote_origin_for_worker(
    std::vector<std::string>* out,
    const std::string& origin_raw
) {
    if (!out) return;

    const std::string origin = trim_copy(origin_raw);
    if (!is_safe_nodus_key_segment_for_worker(origin)) {
        return;
    }

    if (std::find(out->begin(), out->end(), origin) != out->end()) {
        return;
    }

    out->push_back(origin);
}

std::vector<std::string> load_known_remote_origins_from_people_db_for_worker() {
    std::vector<std::string> out;

    sqlite3* db = nullptr;
    if (sqlite3_open(kPeopleContactsDbPathForKnownRemoteOrigins, &db) != SQLITE_OK) {
        if (db) sqlite3_close(db);
        return out;
    }

    sqlite3_busy_timeout(db, 2000);

    const char* schema_sql =
        "CREATE TABLE IF NOT EXISTS known_remote_origins ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "origin_nas TEXT NOT NULL,"
        "public_base_url TEXT NOT NULL DEFAULT '',"
        "display_name TEXT NOT NULL DEFAULT '',"
        "source TEXT NOT NULL DEFAULT '',"
        "source_event_id TEXT NOT NULL DEFAULT '',"
        "added_by_fp TEXT NOT NULL DEFAULT '',"
        "enabled INTEGER NOT NULL DEFAULT 1,"
        "first_seen_epoch INTEGER NOT NULL,"
        "updated_epoch INTEGER NOT NULL,"
        "UNIQUE(origin_nas)"
        ");"
        "CREATE INDEX IF NOT EXISTS idx_known_remote_origins_enabled "
        "ON known_remote_origins(enabled, updated_epoch DESC);";

    char* msg = nullptr;
    if (sqlite3_exec(db, schema_sql, nullptr, nullptr, &msg) != SQLITE_OK) {
        if (msg) sqlite3_free(msg);
        sqlite3_close(db);
        return out;
    }
    if (msg) sqlite3_free(msg);

    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db,
            "SELECT origin_nas FROM known_remote_origins "
            "WHERE enabled = 1 "
            "ORDER BY updated_epoch DESC "
            "LIMIT 200",
            -1, &st, nullptr) != SQLITE_OK) {
        sqlite3_close(db);
        return out;
    }

    while (sqlite3_step(st) == SQLITE_ROW) {
        const char* raw =
            reinterpret_cast<const char*>(sqlite3_column_text(st, 0));
        append_unique_remote_origin_for_worker(&out, raw ? raw : "");
    }

    sqlite3_finalize(st);
    sqlite3_close(db);
    return out;
}

std::vector<std::string> split_remote_origins_env_for_worker(std::string raw) {
    for (char& c : raw) {
        if (c == ';' || std::isspace(static_cast<unsigned char>(c))) {
            c = ',';
        }
    }

    std::vector<std::string> out;
    std::istringstream in(raw);
    std::string part;

    while (std::getline(in, part, ',')) {
        part = trim_copy(part);
        if (part.empty()) continue;
        if (!is_safe_nodus_key_segment_for_worker(part)) {
            std::cerr << "[CircleFederationWorker] ignored unsafe remote origin key segment\n";
            continue;
        }
        if (std::find(out.begin(), out.end(), part) != out.end()) continue;
        out.push_back(part);
    }

    return out;
}



std::string recent_index_json_for_outbox_event(
    const CircleFederationOutboxEvent& current,
    int limit) {
    limit = std::clamp(limit, 1, 100);

    json ids = json::array();
    std::vector<std::string> seen;

    // SIGNED_RECENT_INDEX_PATCH_V1
    auto event_json_is_signed = [](const std::string& raw) {
        json ev = json::parse(raw, nullptr, false);
        return ev.is_object() &&
               ev.contains("origin_nas") &&
               ev.contains("origin_pubkey") &&
               ev.contains("origin_sig") &&
               ev.contains("origin_sig_alg") &&
               ev.value("origin_sig_alg", "") == "Ed25519";
    };

    auto push_id = [&](const std::string& id) {
        if (id.empty()) return;
        if (std::find(seen.begin(), seen.end(), id) != seen.end()) return;
        seen.push_back(id);
        ids.push_back(id);
    };

    if (event_json_is_signed(current.event_json)) {
        push_id(current.event_id);
    }

    std::string err;
    const auto rows = list_circle_federation_outbox(limit, &err);
    if (!err.empty()) {
        std::cerr << "[CircleFederationWorker] recent index list failed: "
                  << err << "\n";
    }

    for (const auto& row : rows) {
        if (static_cast<int>(ids.size()) >= limit) break;
        if (row.circle_id != current.circle_id) continue;
        if (!event_json_is_signed(row.event_json)) continue;
        push_id(row.event_id);
    }

    return ids.dump();
}


bool publish_one_event_to_seeds(
    const CircleFederationOutboxEvent& ev,
    const NodusClientConfig& config,
    const std::vector<NodusSeed>& seeds,
    int recent_slots,
    int recent_index_limit,
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

            NodusCommandResult recent_put{};
            recent_put.exit_code = 0;
            recent_put.output = "skipped because recent_slots=0";

            if (recent_slots > 0) {
                const int recent_slot = recent_slot_for_outbox_id(ev.id, recent_slots);
                const std::string recent_key = circle_recent_key(ev.circle_id, recent_slot);
                recent_put = nodus_cli_put(config, seed, recent_key, ev.event_id);
            }

            const std::string recent_index_key = circle_recent_index_key(ev.circle_id);
            const std::string recent_index_json =
                recent_index_json_for_outbox_event(ev, recent_index_limit);
            const auto recent_index_put =
                nodus_cli_put(config, seed, recent_index_key, recent_index_json);

            NodusCommandResult origin_recent_index_put{};
            origin_recent_index_put.exit_code = 0;
            origin_recent_index_put.output = "skipped: event has no safe origin_nas";

            const std::string origin_nas =
                event_origin_nas_from_json_for_worker(ev.event_json);

            const std::string origin_recent_index_key =
                circle_origin_recent_index_key_for_worker(ev.circle_id, origin_nas);

            if (!origin_recent_index_key.empty()) {
                origin_recent_index_put =
                    nodus_cli_put(config, seed, origin_recent_index_key, recent_index_json);
            }

            if (head_put.exit_code != 0 ||
                recent_put.exit_code != 0 ||
                recent_index_put.exit_code != 0 ||
                origin_recent_index_put.exit_code != 0) {
                all_ok = false;
                errors << seed.name << " event_exit=" << event_put.exit_code
                       << " head_exit=" << head_put.exit_code
                       << " recent_exit=" << recent_put.exit_code
                       << " recent_index_exit=" << recent_index_put.exit_code
                       << " origin_recent_index_exit=" << origin_recent_index_put.exit_code
                       << "\n"
                       << event_put.output << "\n"
                       << head_put.output << "\n"
                       << recent_put.output << "\n"
                       << recent_index_put.output << "\n"
                       << origin_recent_index_put.output << "\n";
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

    if (!is_safe_circle_federation_event_id(event_id)) {
        log_rejected_remote_event_id("head", seed, event_id);
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

    if (event_json_too_large(event_json_raw, event_id, "head")) {
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

    if (!verify_federation_event_signature(event, event_id, "head")) {
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



bool worker_fetch_event_to_inbox(
    const std::string& circle_id,
    const std::string& event_id,
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& local_nodus_fp,
    const char* source_label) {
    if (event_id.empty()) return false;

    if (!is_safe_circle_federation_event_id(event_id)) {
        log_rejected_remote_event_id(source_label, seed, event_id);
        return false;
    }

    const std::string event_key = circle_event_key(circle_id, event_id);

    NodusCommandResult event_get;
    try {
        event_get = nodus_cli_get(config, seed, event_key);
    } catch (const std::exception& e) {
        std::cerr << "[CircleFederationWorker] inbound " << source_label
                  << " event get exception seed=" << seed.name
                  << " event_id=" << event_id << ": " << e.what() << "\n";
        return false;
    }

    const std::string event_json_raw = extract_nodus_value(event_get.output);
    if (event_get.exit_code != 0 || event_json_raw.empty()) {
        return false;
    }

    if (event_json_too_large(event_json_raw, event_id, source_label)) {
        return false;
    }

    json event;
    try {
        event = json::parse(event_json_raw);
    } catch (...) {
        std::cerr << "[CircleFederationWorker] inbound " << source_label
                  << " invalid JSON event_id=" << event_id << "\n";
        return false;
    }

    if (!verify_federation_event_signature(event, event_id, source_label)) {
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
        std::cerr << "[CircleFederationWorker] inbound " << source_label
                  << " store failed event_id=" << event_id
                  << ": " << store_err << "\n";
        return false;
    }

    return true;
}

void worker_pull_recent_index_remote_events(
    const std::string& circle_id,
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& local_nodus_fp,
    int max_items) {
    max_items = std::clamp(max_items, 1, 100);

    const std::string index_key = circle_recent_index_key(circle_id);

    NodusCommandResult index_get;
    try {
        index_get = nodus_cli_get(config, seed, index_key);
    } catch (const std::exception& e) {
        std::cerr << "[CircleFederationWorker] inbound recent:index get exception seed="
                  << seed.name << ": " << e.what() << "\n";
        return;
    }

    const std::string raw = extract_nodus_value(index_get.output);
    if (index_get.exit_code != 0 || raw.empty()) {
        return;
    }

    json ids;
    try {
        ids = json::parse(raw);
    } catch (...) {
        std::cerr << "[CircleFederationWorker] inbound recent:index invalid JSON seed="
                  << seed.name << "\n";
        return;
    }

    if (!ids.is_array()) return;

    int pulled = 0;
    for (const auto& item : ids) {
        if (pulled >= max_items) break;
        if (!item.is_string()) continue;

        const std::string event_id = item.get<std::string>();
        if (worker_fetch_event_to_inbox(
                circle_id,
                event_id,
                config,
                seed,
                local_nodus_fp,
                "recent:index")) {
            ++pulled;
        }
    }

    if (pulled > 0 &&
        env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER_VERBOSE_PULLS")) {
        std::cerr << "[CircleFederationWorker] inbound recent:index pulled="
                  << pulled << " seed=" << seed.name << "\n";
    }
}



void worker_pull_origin_recent_index_remote_events(
    const std::string& circle_id,
    const std::string& origin_nas,
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& local_nodus_fp,
    int max_items) {
    max_items = std::clamp(max_items, 1, 100);

    const std::string index_key =
        circle_origin_recent_index_key_for_worker(circle_id, origin_nas);

    if (index_key.empty()) {
        return;
    }

    NodusCommandResult index_get;
    try {
        index_get = nodus_cli_get(config, seed, index_key);
    } catch (const std::exception& e) {
        std::cerr << "[CircleFederationWorker] inbound origin:recent:index get exception seed="
                  << seed.name << " origin=" << origin_nas << ": " << e.what() << "\n";
        return;
    }

    const std::string raw = extract_nodus_value(index_get.output);
    if (index_get.exit_code != 0 || raw.empty()) {
        return;
    }

    json ids;
    try {
        ids = json::parse(raw);
    } catch (...) {
        std::cerr << "[CircleFederationWorker] inbound origin:recent:index invalid JSON seed="
                  << seed.name << " origin=" << origin_nas << "\n";
        return;
    }

    if (!ids.is_array()) return;

    int pulled = 0;
    for (const auto& item : ids) {
        if (pulled >= max_items) break;
        if (!item.is_string()) continue;

        const std::string event_id = item.get<std::string>();
        if (worker_fetch_event_to_inbox(
                circle_id,
                event_id,
                config,
                seed,
                local_nodus_fp,
                "origin:recent:index")) {
            ++pulled;
        }
    }

    if (pulled > 0 ||
        env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER_VERBOSE_PULLS")) {
        std::cerr << "[CircleFederationWorker] inbound origin:recent:index origin="
                  << origin_nas.substr(0, 12)
                  << " pulled=" << pulled
                  << " seed=" << seed.name << "\n";
    }
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

        if (!is_safe_circle_federation_event_id(event_id)) {
            log_rejected_remote_event_id("recent", seed, event_id);
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

        if (event_json_too_large(event_json_raw, event_id, "recent")) {
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

        if (!verify_federation_event_signature(event, event_id, "recent")) {
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
        local_federation_origin_fingerprint(config);

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

        if (event_json_too_large(row.event_json, row.event_id, "apply")) {
            if (!mark_circle_federation_inbox_failed(
                    row.id,
                    "federation event JSON too large",
                    &mark_err)) {
                std::cerr << "[CircleFederationWorker] inbound mark oversized JSON failed id="
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

        if (!verify_federation_event_signature(event, row.event_id, "apply")) {
            if (!mark_circle_federation_inbox_failed(
                    row.id,
                    "invalid_event_signature",
                    &mark_err)) {
                std::cerr << "[CircleFederationWorker] inbound mark invalid signature failed id="
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

        bool local_post_reaction_reduced = false;
        std::string reduce_err;
        if (!worker_reduce_federated_reaction_to_local_post_if_targeted(
                row,
                event,
                local_nodus_fp,
                &local_post_reaction_reduced,
                &reduce_err)) {
            if (!mark_circle_federation_inbox_failed(
                    row.id,
                    reduce_err.empty()
                        ? "federated_reaction_worker_reduce_failed"
                        : reduce_err,
                    &mark_err)) {
                std::cerr << "[CircleFederationWorker] inbound mark reducer failed id="
                          << row.id << ": " << mark_err << "\n";
            } else {
                std::cerr << "[CircleFederationWorker] inbound reducer failed event_id="
                          << row.event_id << ": "
                          << (reduce_err.empty()
                                ? "federated_reaction_worker_reduce_failed"
                                : reduce_err)
                          << "\n";
            }
            continue;
        }

        if (local_post_reaction_reduced &&
            env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER_VERBOSE_PULLS")) {
            std::cerr << "[CircleFederationWorker] inbound reduced local post reaction event_id="
                      << row.event_id << "\n";
        }

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

// SKIP_GLOBAL_RECENT_INDEX_WHEN_REMOTE_ORIGINS_PATCH_V1
void worker_pull_and_apply_inbound(
    const std::string& circle_id,
    const NodusClientConfig& config,
    const std::vector<NodusSeed>& seeds,
    int batch_limit,
    int recent_slots,
    int recent_index_limit,
    bool poll_legacy_head,
    bool poll_global_recent_index,
    const std::vector<std::string>& remote_origins) {
    (void)poll_global_recent_index;
    const std::string local_nodus_fp =
        local_federation_origin_fingerprint(config);

    // KNOWN_REMOTE_ORIGINS_EFFECTIVE_LOG_PATCH_V1
    const auto db_remote_origins =
        load_known_remote_origins_from_people_db_for_worker();

    std::vector<std::string> effective_remote_origins = remote_origins;
    for (const auto& origin : db_remote_origins) {
        append_unique_remote_origin_for_worker(&effective_remote_origins, origin);
    }

    const bool effective_poll_global_recent_index =
        env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER_POLL_GLOBAL_RECENT_INDEX") ||
        effective_remote_origins.empty();

    static int last_env_origin_count = -1;
    static int last_db_origin_count = -1;
    static int last_effective_origin_count = -1;
    static int last_effective_poll_global = -1;

    const int env_origin_count =
        static_cast<int>(remote_origins.size());
    const int db_origin_count =
        static_cast<int>(db_remote_origins.size());
    const int effective_origin_count =
        static_cast<int>(effective_remote_origins.size());
    const int effective_poll_global_int =
        effective_poll_global_recent_index ? 1 : 0;

    if (env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER_VERBOSE_PULLS") ||
        env_origin_count != last_env_origin_count ||
        db_origin_count != last_db_origin_count ||
        effective_origin_count != last_effective_origin_count ||
        effective_poll_global_int != last_effective_poll_global) {
        std::cerr << "[CircleFederationWorker] inbound origins env="
                  << env_origin_count
                  << " db=" << db_origin_count
                  << " effective=" << effective_origin_count
                  << " poll_global_recent_index=" << effective_poll_global_int
                  << "\n";

        last_env_origin_count = env_origin_count;
        last_db_origin_count = db_origin_count;
        last_effective_origin_count = effective_origin_count;
        last_effective_poll_global = effective_poll_global_int;
    }

    // First clear anything already discovered in a previous loop.
    worker_apply_pending_inbox(config, batch_limit);

    for (const auto& seed : seeds) {
        if (poll_legacy_head) {
            worker_pull_latest_remote_head(circle_id, config, seed, local_nodus_fp);
        }

        if (effective_poll_global_recent_index) {
            worker_pull_recent_index_remote_events(
                circle_id,
                config,
                seed,
                local_nodus_fp,
                recent_index_limit);
        }

        for (const auto& remote_origin : effective_remote_origins) {
            worker_pull_origin_recent_index_remote_events(
                circle_id,
                remote_origin,
                config,
                seed,
                local_nodus_fp,
                recent_index_limit);
        }

        if (recent_slots > 0) {
            worker_pull_recent_remote_events(circle_id, config, seed, local_nodus_fp, recent_slots);
        }
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
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_RECENT_SLOTS", 0, 0, 512);
    const int recent_index_limit =
        env_int("PQNAS_CIRCLE_FEDERATION_WORKER_RECENT_INDEX_LIMIT", 20, 1, 100);
    const bool poll_legacy_head =
        env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER_POLL_LEGACY_HEAD");
    const std::string remote_origins_raw =
        env_string("PQNAS_CIRCLE_FEDERATION_WORKER_REMOTE_ORIGINS", "");
    const auto remote_origins =
        split_remote_origins_env_for_worker(remote_origins_raw);
    const bool poll_global_recent_index =
        remote_origins.empty() ||
        env_enabled("PQNAS_CIRCLE_FEDERATION_WORKER_POLL_GLOBAL_RECENT_INDEX");
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
              << " recent_index_limit=" << recent_index_limit
              << " poll_legacy_head=" << (poll_legacy_head ? "1" : "0")
              << " poll_global_recent_index=" << (poll_global_recent_index ? "1" : "0")
              << " remote_origins=" << remote_origins.size()
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

                if (publish_one_event_to_seeds(row, config, seeds, recent_slots, recent_index_limit, &publish_err)) {
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
                recent_slots,
                recent_index_limit,
                poll_legacy_head,
                poll_global_recent_index,
                remote_origins);
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
