#include "routes_circle_nodus_research.h"

#include "federation/circle_federation_event.h"
#include "federation/circle_federation_outbox.h"
#include "federation/circle_federation_outbox_worker.h"
#include "federation/circle_federation_inbox.h"
#include "federation/circle_federation_remote_feed.h"
#include "federation/circle_federation_signing.h"
#include "federation/pqnas_nodus_client.h"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <fstream>
#include <cstdio>
#include <array>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fcntl.h>
#include <iomanip>
#include <netdb.h>
#include <random>
#include <sstream>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace pqnas {

namespace {

using json = nlohmann::json;

void set_json(httplib::Response& res, int status, const json& body) {
    res.status = status;
    res.set_content(body.dump(), "application/json; charset=utf-8");
}

std::string trim_copy(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

std::string clamp_output(std::string s) {
    s = trim_copy(std::move(s));
    constexpr std::size_t max_len = 4096;
    if (s.size() > max_len) {
        s.resize(max_len);
        s += "\n...[truncated]";
    }
    return s;
}

json command_result_json(const federation::NodusCommandResult& r) {
    return {
        {"ok", r.exit_code == 0},
        {"exit_code", r.exit_code},
        {"output", clamp_output(r.output)}
    };
}

std::string json_value_to_string(const json& body, const char* key) {
    if (!body.contains(key)) return "";
    const auto& v = body.at(key);
    if (v.is_string()) return v.get<std::string>();
    if (v.is_null()) return "";
    return v.dump();
}

bool parse_json_body(const httplib::Request& req, httplib::Response& res, json* out) {
    if (!out) return false;

    if (req.body.empty()) {
        *out = json::object();
        return true;
    }

    try {
        *out = json::parse(req.body);
    } catch (...) {
        set_json(res, 400, {{"ok", false}, {"error", "invalid_json"}});
        return false;
    }

    if (!out->is_object()) {
        set_json(res, 400, {
            {"ok", false},
            {"error", "invalid_json"},
            {"message", "request body must be a JSON object"}
        });
        return false;
    }

    return true;
}

bool require_admin(const CircleNodusResearchRoutesDeps& deps,
                   const httplib::Request& req,
                   httplib::Response& res,
                   std::string* actor_fp_out) {
    if (!deps.users || !deps.cookie_key || !deps.require_user_auth_users_actor) {
        set_json(res, 500, {
            {"ok", false},
            {"error", "server_error"},
            {"message", "Nodus research route dependencies missing"}
        });
        return false;
    }

    std::string actor_fp;
    std::string actor_role;

    if (!deps.require_user_auth_users_actor(
            req, res, deps.cookie_key, deps.users, &actor_fp, &actor_role)) {
        return false;
    }

    if (actor_role != "admin") {
        set_json(res, 403, {{"ok", false}, {"error", "forbidden"}});
        return false;
    }

    if (actor_fp_out) *actor_fp_out = actor_fp;
    return true;
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

bool verify_admin_federation_event_signature(
    const json& event,
    const std::string& event_id,
    std::string* err) {
    if (err) *err = "";

    if (!event.is_object()) {
        if (err) *err = "event_not_object";
        return false;
    }

    const std::string origin_nas = event.value("origin_nas", "");
    const std::string origin_pubkey = event.value("origin_pubkey", "");
    const std::string origin_sig = event.value("origin_sig", "");
    const std::string origin_sig_alg = event.value("origin_sig_alg", "");

    if (origin_nas.empty() ||
        origin_pubkey.empty() ||
        origin_sig.empty() ||
        origin_sig_alg != "Ed25519") {
        if (err) *err = "unsigned_or_unsupported_event_signature";
        return false;
    }

    const std::string signing_fp =
        federation::circle_federation_signing_public_key_fingerprint(origin_pubkey);

    if (signing_fp.empty() || signing_fp != origin_nas) {
        if (err) *err = "signing_key_fingerprint_mismatch";
        return false;
    }

    const std::string canonical =
        canonical_federation_event_without_signature(event);

    std::string verify_err;
    if (!federation::verify_circle_federation_canonical_json(
            origin_pubkey,
            canonical,
            origin_sig,
            &verify_err)) {
        if (err) *err = verify_err.empty() ? "invalid_event_signature" : verify_err;
        return false;
    }

    (void)event_id;
    return true;
}

std::string local_federation_origin_fingerprint(
    const federation::NodusClientConfig& config) {
    federation::CircleFederationSigningIdentity identity;
    std::string err;

    if (federation::ensure_circle_federation_signing_identity(
            config.identity_dir,
            &identity,
            &err)) {
        return identity.public_key_fingerprint;
    }

    return nodus_identity_fingerprint_from_dir(config.identity_dir);
}

std::string short_fp(const std::string& fp, std::size_t n = 16) {
    if (fp.size() <= n) return fp;
    return fp.substr(0, n);
}

bool truthy_env(const char* name) {
    const char* raw = std::getenv(name);
    if (!raw || !raw[0]) return false;

    const std::string v = lower_copy(trim_copy(raw));
    return v == "1" || v == "true" || v == "yes" || v == "y" || v == "on";
}

std::string env_string(const char* name) {
    const char* raw = std::getenv(name);
    return raw && raw[0] ? std::string(raw) : "";
}

std::string public_base_url_from_env() {
    std::string v = env_string("PQNAS_PUBLIC_BASE_URL");
    if (!v.empty()) return v;

    v = env_string("PQNAS_ORIGIN");
    if (!v.empty()) return v;

    return "";
}

std::string run_shell_capture_limited(const std::string& cmd, std::size_t max_len = 4096) {
    std::array<char, 256> buf{};
    std::string out;

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";

    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        out += buf.data();
        if (out.size() >= max_len) {
            out.resize(max_len);
            out += "\n...[truncated]";
            break;
        }
    }

    pclose(pipe);
    return trim_copy(out);
}

std::string first_nonempty_line(const std::string& raw) {
    std::istringstream in(raw);
    std::string line;

    while (std::getline(in, line)) {
        line = trim_copy(line);
        if (!line.empty()) return line;
    }

    return "";
}

std::string nodus_cli_version_line(const federation::NodusClientConfig& config) {
    if (config.nodus_cli_path.empty()) return "";

    const std::string cmd =
        "timeout 3 " +
        federation::shell_quote_for_nodus_research(config.nodus_cli_path) +
        " 2>&1";

    return first_nonempty_line(run_shell_capture_limited(cmd, 2048));
}


federation::NodusClientConfig make_nodus_config() {
    federation::NodusClientConfig config;

    if (const char* p = std::getenv("PQNAS_NODUS_CLI")) {
        if (p[0]) config.nodus_cli_path = p;
    }

    const std::string production_identity_dir = "/srv/pqnas/config/nodus/identity";
    const std::string legacy_identity_dir = "/srv/pqnas/config/nodus/research_identity";

    config.identity_dir = production_identity_dir;

    if (const char* p = std::getenv("PQNAS_NODUS_IDENTITY_DIR")) {
        if (p[0]) {
            config.identity_dir = p;
        }
    } else if (nodus_identity_fingerprint_from_dir(production_identity_dir).empty() &&
               !nodus_identity_fingerprint_from_dir(legacy_identity_dir).empty()) {
        config.identity_dir = legacy_identity_dir;
    }

    if (const char* p = std::getenv("PQNAS_NODUS_TIMEOUT_SECONDS")) {
        try {
            config.timeout_seconds = std::clamp(std::stoi(p), 1, 60);
        } catch (...) {
            config.timeout_seconds = 8;
        }
    }

    return config;
}

bool ensure_identity_dir(const federation::NodusClientConfig& config, std::string* err) {
    if (config.identity_dir.empty()) return true;

    std::error_code ec;
    std::filesystem::create_directories(config.identity_dir, ec);
    if (ec) {
        if (err) *err = ec.message();
        return false;
    }

    return true;
}

json seed_json(const federation::NodusSeed& seed) {
    return {
        {"name", seed.name},
        {"host", seed.host},
        {"client_port", seed.client_port}
    };
}

std::vector<federation::NodusSeed> select_seeds(
    const std::string& selector_raw,
    bool default_all,
    std::string* err) {
    const auto seeds = federation::default_nodus_seeds();
    const std::string selector = trim_copy(selector_raw);

    if (selector.empty()) {
        if (default_all) return seeds;
        if (!seeds.empty()) return {seeds.front()};
        return {};
    }

    const std::string needle = lower_copy(selector);
    if (needle == "all") return seeds;

    std::vector<federation::NodusSeed> out;
    for (const auto& seed : seeds) {
        if (lower_copy(seed.name) == needle || lower_copy(seed.host) == needle) {
            out.push_back(seed);
        }
    }

    if (out.empty() && err) *err = "unknown Nodus seed: " + selector;
    return out;
}

bool valid_research_key(const std::string& key) {
    return !key.empty() && key.size() <= 512 && starts_with(key, "pqnas:");
}

std::string now_iso_utc() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&now, &tm);

    char buf[32]{};
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return std::string(buf);
}

std::string make_event_id() {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();

    std::random_device rd;

    std::ostringstream out;
    out << "evt_" << micros << "_"
        << std::hex << std::setw(8) << std::setfill('0') << rd();

    return out.str();
}

bool tcp_check_one_addr(const addrinfo* ai, int timeout_ms, std::string* err) {
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        if (err) *err = std::strerror(errno);
        return false;
    }

    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (err) *err = "fcntl failed";
        close(fd);
        return false;
    }

    if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
        close(fd);
        return true;
    }

    if (errno != EINPROGRESS) {
        if (err) *err = std::strerror(errno);
        close(fd);
        return false;
    }

    fd_set wfds;
    FD_ZERO(&wfds);
    FD_SET(fd, &wfds);

    timeval tv{};
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    const int selected = select(fd + 1, nullptr, &wfds, nullptr, &tv);
    if (selected <= 0) {
        if (err) *err = selected == 0 ? "timeout" : std::strerror(errno);
        close(fd);
        return false;
    }

    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0) {
        if (err) *err = "getsockopt failed";
        close(fd);
        return false;
    }

    close(fd);

    if (so_error != 0) {
        if (err) *err = std::strerror(so_error);
        return false;
    }

    return true;
}

bool tcp_check(const std::string& host, int port, int timeout_ms, std::string* err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addrs = nullptr;
    const std::string port_s = std::to_string(port);

    const int rc = getaddrinfo(host.c_str(), port_s.c_str(), &hints, &addrs);
    if (rc != 0) {
        if (err) *err = gai_strerror(rc);
        return false;
    }

    std::string last_err = "connect failed";
    bool ok = false;

    for (const addrinfo* ai = addrs; ai; ai = ai->ai_next) {
        std::string one_err;
        if (tcp_check_one_addr(ai, timeout_ms, &one_err)) {
            ok = true;
            break;
        }
        if (!one_err.empty()) last_err = one_err;
    }

    freeaddrinfo(addrs);

    if (!ok && err) *err = last_err;
    return ok;
}

int query_int(const httplib::Request& req,
              const std::string& name,
              int fallback,
              int lo,
              int hi) {
    if (!req.has_param(name)) return fallback;

    try {
        return std::clamp(std::stoi(req.get_param_value(name)), lo, hi);
    } catch (...) {
        return fallback;
    }
}



std::string outbox_publish_error_summary(const json& result) {
    std::string out;

    if (result.contains("put_event")) {
        out += result["put_event"].value("output", "");
    }
    if (result.contains("put_head")) {
        if (!out.empty()) out += "\n";
        out += result["put_head"].value("output", "");
    }

    if (out.empty()) out = "unknown publish error";

    constexpr std::size_t kMax = 2048;
    if (out.size() > kMax) {
        out.resize(kMax);
        out += "...[truncated]";
    }

    return out;
}

int outbox_retry_delay_seconds(int attempts) {
    int delay = 30;
    for (int i = 1; i < attempts && delay < 3600; ++i) {
        delay *= 2;
    }
    return std::clamp(delay, 30, 3600);
}



json remote_feed_event_json(const federation::CircleFederationRemoteFeedEvent& ev) {
    return {
        {"id", ev.id},
        {"received_epoch", ev.received_epoch},
        {"created_epoch", ev.created_epoch},
        {"circle_id", ev.circle_id},
        {"event_id", ev.event_id},
        {"event_type", ev.event_type},
        {"origin_nas", ev.origin_nas},
        {"target_type", ev.target_type},
        {"post_id", ev.post_id},
        {"reply_id", ev.reply_id},
        {"actor_fp", ev.actor_fp},
        {"reaction", ev.reaction}
    };
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
            // circle.post.created uses owner_fp, while reactions/replies use actor_fp.
            *actor_fp = payload["owner_fp"].get<std::string>();
        }
    }

    if (reaction && payload.contains("reaction") && payload["reaction"].is_string()) {
        *reaction = payload["reaction"].get<std::string>();
    }

    return true;
}

json inbox_event_json(const federation::CircleFederationInboxEvent& ev) {
    return {
        {"id", ev.id},
        {"received_epoch", ev.received_epoch},
        {"created_epoch", ev.created_epoch},
        {"status", ev.status},
        {"circle_id", ev.circle_id},
        {"event_id", ev.event_id},
        {"event_type", ev.event_type},
        {"origin_nas", ev.origin_nas},
        {"event_key", ev.event_key},
        {"last_error", ev.last_error}
    };
}


// FEDERATED_REACTION_REDUCER_PATCH_V1

static constexpr const char* kCircleStackDbPathForFederationReducer =
    "/srv/pqnas/circlestack.db";

bool federation_reducer_supported_reaction(const std::string& reaction) {
    return reaction == "👍" ||
           reaction == "❤️" ||
           reaction == "😂" ||
           reaction == "😮" ||
           reaction == "👏" ||
           reaction == "🔥";
}

std::string federation_reducer_short_fp(const std::string& fp) {
    return fp.size() >= 8 ? fp.substr(0, 8) : fp;
}

long long federation_reducer_post_id_from_event_id(const std::string& event_id) {
    const std::string prefix = "post_";
    if (!starts_with(event_id, prefix)) return 0;

    const auto pos = event_id.rfind('_');
    if (pos == std::string::npos || pos + 1 >= event_id.size()) return 0;

    try {
        const long long id = std::stoll(event_id.substr(pos + 1));
        return id > 0 ? id : 0;
    } catch (...) {
        return 0;
    }
}

std::string federation_reducer_expected_post_event_id(
    long long post_id,
    long long created_epoch
) {
    return "post_" + std::to_string(created_epoch) + "_" + std::to_string(post_id);
}

bool federation_reducer_ensure_table(sqlite3* db, std::string* err) {
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

bool federation_reducer_validate_local_post_target(
    sqlite3* db,
    const std::string& target_event_id,
    long long* out_post_id,
    std::string* err
) {
    if (out_post_id) *out_post_id = 0;

    const long long post_id =
        federation_reducer_post_id_from_event_id(target_event_id);

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
        federation_reducer_expected_post_event_id(post_id, created_epoch);

    if (target_event_id != expected) {
        if (err) *err = "target_event_id_mismatch";
        return false;
    }

    if (out_post_id) *out_post_id = post_id;
    return true;
}

bool federation_reducer_apply_created(
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
        actor_display_name = federation_reducer_short_fp(actor_fp) + " · remote NAS";
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

bool federation_reducer_apply_removed(
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

bool apply_federated_reaction_to_local_post_if_targeted(
    const federation::CircleFederationInboxEvent& row,
    const json& event,
    const std::string& local_nodus_fp,
    bool* out_touched_local_post,
    std::string* err
) {
    if (out_touched_local_post) *out_touched_local_post = false;
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

    if (out_touched_local_post) *out_touched_local_post = true;

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
        !federation_reducer_supported_reaction(reaction)) {
        if (err) *err = "unsupported_reaction";
        return false;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open(kCircleStackDbPathForFederationReducer, &db) != SQLITE_OK) {
        if (err) *err = db ? sqlite3_errmsg(db) : "sqlite3_open_failed";
        if (db) sqlite3_close(db);
        return false;
    }

    sqlite3_busy_timeout(db, 5000);

    bool ok = true;
    std::string local_err;

    if (!federation_reducer_ensure_table(db, &local_err)) {
        ok = false;
    }

    long long local_post_id = 0;
    if (ok &&
        !federation_reducer_validate_local_post_target(
            db,
            target_event_id,
            &local_post_id,
            &local_err)) {
        ok = false;
    }

    const long long event_epoch =
        event.value("created_epoch", row.created_epoch);

    if (ok && event_type == "circle.reaction.created") {
        ok = federation_reducer_apply_created(
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
        ok = federation_reducer_apply_removed(
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
        *err = local_err.empty() ? "federated_reaction_reduce_failed" : local_err;
    }

    return ok;
}

std::string extract_nodus_value(const std::string& output) {
    const std::string marker = "Value: ";
    const auto pos = output.find(marker);
    if (pos == std::string::npos) return "";

    const auto start = pos + marker.size();
    const auto end = output.find('\n', start);

    if (end == std::string::npos) {
        return output.substr(start);
    }

    return output.substr(start, end - start);
}


std::string recent_index_json_for_outbox_event(
    const federation::CircleFederationOutboxEvent& current,
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
    const auto rows = federation::list_circle_federation_outbox(limit, &err);

    for (const auto& row : rows) {
        if (static_cast<int>(ids.size()) >= limit) break;
        if (row.circle_id != current.circle_id) continue;
        if (!event_json_is_signed(row.event_json)) continue;
        push_id(row.event_id);
    }

    return ids.dump();
}


json outbox_event_json(const federation::CircleFederationOutboxEvent& ev) {
    return {
        {"id", ev.id},
        {"created_epoch", ev.created_epoch},
        {"updated_epoch", ev.updated_epoch},
        {"next_attempt_epoch", ev.next_attempt_epoch},
        {"attempts", ev.attempts},
        {"status", ev.status},
        {"event_type", ev.event_type},
        {"circle_id", ev.circle_id},
        {"event_id", ev.event_id},
        {"event_key", ev.event_key},
        {"head_key", ev.head_key},
        {"last_error", ev.last_error}
    };
}

} // namespace

void register_circle_nodus_research_routes(
    httplib::Server& server,
    const CircleNodusResearchRoutesDeps& deps) {
    federation::start_circle_federation_outbox_worker_once();
    server.Get("/api/v4/admin/nodus/status",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            const int timeout_ms = query_int(req, "timeout_ms", 1500, 100, 10000);
            const auto config = make_nodus_config();

            const bool cli_installed =
                !config.nodus_cli_path.empty() &&
                std::filesystem::is_regular_file(config.nodus_cli_path);

            const std::string version_line =
                cli_installed ? nodus_cli_version_line(config) : "";

            const std::string fingerprint =
                nodus_identity_fingerprint_from_dir(config.identity_dir);

            const std::string legacy_identity_dir =
                "/srv/pqnas/config/nodus/research_identity";

            const bool identity_exists = !fingerprint.empty();
            const bool legacy_exists =
                !nodus_identity_fingerprint_from_dir(legacy_identity_dir).empty();

            int reachable_count = 0;
            json seeds = json::array();

            for (const auto& seed : federation::default_nodus_seeds()) {
                std::string err;
                const bool reachable =
                    tcp_check(seed.host, seed.client_port, timeout_ms, &err);

                if (reachable) ++reachable_count;

                json item = seed_json(seed);
                item["reachable"] = reachable;
                item["error"] = reachable ? "" : err;
                seeds.push_back(item);
            }

            const bool worker_enabled =
                truthy_env("PQNAS_CIRCLE_FEDERATION_WORKER");

            const std::string worker_env =
                env_string("PQNAS_CIRCLE_FEDERATION_WORKER");

            const std::string public_base_url =
                public_base_url_from_env();

            set_json(res, 200, {
                {"ok", true},

                // Backwards-compatible flat fields.
                {"nodus_cli_path", config.nodus_cli_path},
                {"identity_dir", config.identity_dir},
                {"timeout_seconds", config.timeout_seconds},
                {"timeout_ms", timeout_ms},
                {"seeds", seeds},

                // Rich status for Admin Settings UI.
                {"cli", {
                    {"path", config.nodus_cli_path},
                    {"installed", cli_installed},
                    {"version", version_line}
                }},
                {"identity", {
                    {"dir", config.identity_dir},
                    {"exists", identity_exists},
                    {"fingerprint", fingerprint},
                    {"fingerprint_short", short_fp(fingerprint)},
                    {"legacy_dir", legacy_identity_dir},
                    {"legacy_exists", legacy_exists}
                }},
                {"public_base_url", public_base_url},
                {"worker", {
                    {"enabled", worker_enabled},
                    {"env", "PQNAS_CIRCLE_FEDERATION_WORKER"},
                    {"raw", worker_env}
                }},
                {"seeds_summary", {
                    {"reachable", reachable_count},
                    {"total", seeds.size()}
                }}
            });
        });

    server.Post("/api/v4/admin/nodus/identity/init",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            const auto config = make_nodus_config();

            if (config.nodus_cli_path.empty() ||
                !std::filesystem::is_regular_file(config.nodus_cli_path)) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "nodus_cli_missing"},
                    {"path", config.nodus_cli_path}
                });
                return;
            }

            const std::string existing_fp =
                nodus_identity_fingerprint_from_dir(config.identity_dir);

            if (!existing_fp.empty()) {
                set_json(res, 200, {
                    {"ok", true},
                    {"created", false},
                    {"already_exists", true},
                    {"identity_dir", config.identity_dir},
                    {"fingerprint", existing_fp},
                    {"fingerprint_short", short_fp(existing_fp)}
                });
                return;
            }

            std::error_code ec;
            std::filesystem::create_directories(config.identity_dir, ec);
            if (ec) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "identity_dir_create_failed"},
                    {"identity_dir", config.identity_dir},
                    {"message", ec.message()}
                });
                return;
            }

            const std::string cmd =
                "timeout " + std::to_string(config.timeout_seconds) + " " +
                federation::shell_quote_for_nodus_research(config.nodus_cli_path) +
                " -i " +
                federation::shell_quote_for_nodus_research(config.identity_dir) +
                " identity-init 2>&1";

            const std::string output = run_shell_capture_limited(cmd, 4096);
            const std::string fp =
                nodus_identity_fingerprint_from_dir(config.identity_dir);

            if (fp.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "identity_init_failed"},
                    {"identity_dir", config.identity_dir},
                    {"output", output}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"created", true},
                {"identity_dir", config.identity_dir},
                {"fingerprint", fp},
                {"fingerprint_short", short_fp(fp)},
                {"output", output}
            });
        });



    server.Get("/api/v4/admin/nodus/remote-feed/stats",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            std::string err;
            const auto stats = federation::circle_federation_remote_feed_stats(&err);
            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "remote_feed_error"},
                    {"message", err}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"total", stats.total},
                {"posts", stats.posts},
                {"replies", stats.replies},
                {"reaction_created", stats.reaction_created},
                {"reaction_removed", stats.reaction_removed}
            });
        });

    server.Get("/api/v4/admin/nodus/remote-feed/list",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            const int limit = query_int(req, "limit", 50, 1, 500);

            std::string err;
            const auto rows = federation::list_circle_federation_remote_feed(limit, &err);
            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "remote_feed_error"},
                    {"message", err}
                });
                return;
            }

            json events = json::array();
            for (const auto& row : rows) {
                events.push_back(remote_feed_event_json(row));
            }

            set_json(res, 200, {
                {"ok", true},
                {"count", events.size()},
                {"events", events}
            });
        });

    server.Post("/api/v4/admin/nodus/inbox/apply-once",
        [deps](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            if (!require_admin(deps, req, res, &actor_fp)) return;

            json body;
            if (!parse_json_body(req, res, &body)) return;

            int limit = 10;
            if (body.contains("limit") && body["limit"].is_number_integer()) {
                limit = std::clamp(body["limit"].get<int>(), 1, 100);
            }

            std::string err;
            const auto rows = federation::list_circle_federation_inbox(limit, &err);
            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "inbox_list_failed"},
                    {"message", err}
                });
                return;
            }

            const auto nodus_config = make_nodus_config();
            const std::string local_nodus_fp =
                local_federation_origin_fingerprint(nodus_config);

            json events = json::array();
            int applied = 0;
            int ignored = 0;
            int failed = 0;

            for (const auto& row : rows) {
                if (row.status != "pending") continue;

                json item = inbox_event_json(row);
                std::string mark_err;

                const bool local_origin =
                    (!local_nodus_fp.empty() && row.origin_nas == local_nodus_fp) ||
                    (local_nodus_fp.empty() && row.origin_nas == actor_fp);

                if (local_origin) {
                    if (federation::mark_circle_federation_inbox_ignored(
                            row.id,
                            "ignored_local_origin",
                            &mark_err)) {
                        item["final_status"] = "ignored";
                        item["reason"] = "ignored_local_origin";
                        ++ignored;
                    } else {
                        item["final_status"] = "failed";
                        item["mark_error"] = mark_err;
                        ++failed;
                    }

                    events.push_back(item);
                    continue;
                }

                json event;
                try {
                    event = json::parse(row.event_json);
                } catch (...) {
                    if (federation::mark_circle_federation_inbox_failed(
                            row.id,
                            "invalid_event_json",
                            &mark_err)) {
                        item["final_status"] = "failed";
                        item["reason"] = "invalid_event_json";
                    } else {
                        item["final_status"] = "failed";
                        item["mark_error"] = mark_err;
                    }

                    ++failed;
                    events.push_back(item);
                    continue;
                }

                std::string sig_err;
                if (!verify_admin_federation_event_signature(
                        event,
                        row.event_id,
                        &sig_err)) {
                    if (federation::mark_circle_federation_inbox_failed(
                            row.id,
                            "invalid_event_signature",
                            &mark_err)) {
                        item["final_status"] = "failed";
                        item["reason"] = "invalid_event_signature";
                        item["message"] = sig_err;
                    } else {
                        item["final_status"] = "failed";
                        item["mark_error"] = mark_err;
                    }

                    ++failed;
                    events.push_back(item);
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

                bool touched_local_post = false;
                std::string reduce_err;
                if (!apply_federated_reaction_to_local_post_if_targeted(
                        row,
                        event,
                        local_nodus_fp,
                        &touched_local_post,
                        &reduce_err)) {
                    if (federation::mark_circle_federation_inbox_failed(
                            row.id,
                            reduce_err.empty() ? "federated_reaction_reduce_failed" : reduce_err,
                            &mark_err)) {
                        item["final_status"] = "failed";
                        item["reason"] = reduce_err.empty()
                            ? "federated_reaction_reduce_failed"
                            : reduce_err;
                    } else {
                        item["final_status"] = "failed";
                        item["mark_error"] = mark_err;
                    }

                    ++failed;
                    events.push_back(item);
                    continue;
                }

                if (touched_local_post) {
                    item["local_post_reaction_reduced"] = true;
                }

                std::string store_err;
                if (!federation::store_circle_federation_remote_feed_event(
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
                    if (federation::mark_circle_federation_inbox_failed(
                            row.id,
                            store_err,
                            &mark_err)) {
                        item["final_status"] = "failed";
                        item["reason"] = store_err;
                    } else {
                        item["final_status"] = "failed";
                        item["mark_error"] = mark_err;
                    }

                    ++failed;
                    events.push_back(item);
                    continue;
                }

                if (federation::mark_circle_federation_inbox_applied(row.id, &mark_err)) {
                    item["final_status"] = "applied";
                    item["remote_feed_stored"] = true;
                    ++applied;
                } else {
                    item["final_status"] = "failed";
                    item["mark_error"] = mark_err;
                    ++failed;
                }

                events.push_back(item);
            }

            set_json(res, 200, {
                {"ok", failed == 0},
                {"applied", applied},
                {"ignored", ignored},
                {"failed", failed},
                {"events", events}
            });
        });

    server.Get("/api/v4/admin/nodus/inbox/stats",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            std::string err;
            const auto stats = federation::circle_federation_inbox_stats(&err);
            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "inbox_error"},
                    {"message", err}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"total", stats.total},
                {"pending", stats.pending},
                {"applied", stats.applied},
                {"ignored", stats.ignored},
                {"failed", stats.failed}
            });
        });

    server.Get("/api/v4/admin/nodus/inbox/list",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            const int limit = query_int(req, "limit", 50, 1, 500);

            std::string err;
            const auto rows = federation::list_circle_federation_inbox(limit, &err);
            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "inbox_error"},
                    {"message", err}
                });
                return;
            }

            json events = json::array();
            for (const auto& row : rows) {
                events.push_back(inbox_event_json(row));
            }

            set_json(res, 200, {
                {"ok", true},
                {"count", events.size()},
                {"events", events}
            });
        });


    server.Post("/api/v4/admin/nodus/inbox/pull-event",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            json body;
            if (!parse_json_body(req, res, &body)) return;

            std::string circle_id = json_value_to_string(body, "circle_id");
            std::string event_id = json_value_to_string(body, "event_id");

            if (circle_id.empty() || event_id.empty()) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "missing_circle_or_event_id"}
                });
                return;
            }

            const std::string seed_selector =
                json_value_to_string(body, "seed").empty()
                    ? "EU-1"
                    : json_value_to_string(body, "seed");

            std::string seed_err;
            const auto seeds = select_seeds(seed_selector, false, &seed_err);
            if (seeds.empty()) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_seed"},
                    {"message", seed_err.empty() ? "no Nodus seeds configured" : seed_err}
                });
                return;
            }

            const auto config = make_nodus_config();
            std::string identity_err;
            if (!ensure_identity_dir(config, &identity_err)) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "identity_dir_error"},
                    {"message", identity_err}
                });
                return;
            }

            const auto& seed = seeds.front();
            const std::string event_key = federation::circle_event_key(circle_id, event_id);

            federation::NodusCommandResult event_get;
            try {
                event_get = federation::nodus_cli_get(config, seed, event_key);
            } catch (const std::exception& e) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "nodus_event_get_failed"},
                    {"message", e.what()}
                });
                return;
            }

            const std::string event_json_raw = extract_nodus_value(event_get.output);
            if (event_get.exit_code != 0 || event_json_raw.empty()) {
                set_json(res, 200, {
                    {"ok", false},
                    {"error", "event_not_found"},
                    {"event_key", event_key},
                    {"event_get", command_result_json(event_get)}
                });
                return;
            }

            json event;
            try {
                event = json::parse(event_json_raw);
            } catch (...) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_event_json"},
                    {"event_key", event_key},
                    {"raw", event_json_raw}
                });
                return;
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
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_event_fields"},
                    {"expected_circle_id", circle_id},
                    {"expected_event_id", event_id},
                    {"event", event}
                });
                return;
            }

            std::string store_err;
            if (!federation::store_circle_federation_inbox_event(
                    parsed_circle_id,
                    parsed_event_id,
                    event_type,
                    origin_nas,
                    created_epoch,
                    event_key,
                    event_json_raw,
                    &store_err)) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "inbox_store_failed"},
                    {"message", store_err}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"stored", true},
                {"circle_id", parsed_circle_id},
                {"event_id", parsed_event_id},
                {"event_type", event_type},
                {"origin_nas", origin_nas},
                {"event_key", event_key},
                {"event_get", command_result_json(event_get)},
                {"event", event}
            });
        });

    server.Post("/api/v4/admin/nodus/inbox/pull-once",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            json body;
            if (!parse_json_body(req, res, &body)) return;

            std::string circle_id = json_value_to_string(body, "circle_id");
            if (circle_id.empty()) circle_id = "local-public-feed";

            const std::string seed_selector =
                json_value_to_string(body, "seed").empty()
                    ? "EU-1"
                    : json_value_to_string(body, "seed");

            std::string seed_err;
            const auto seeds = select_seeds(seed_selector, false, &seed_err);
            if (seeds.empty()) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_seed"},
                    {"message", seed_err.empty() ? "no Nodus seeds configured" : seed_err}
                });
                return;
            }

            const auto config = make_nodus_config();
            std::string identity_err;
            if (!ensure_identity_dir(config, &identity_err)) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "identity_dir_error"},
                    {"message", identity_err}
                });
                return;
            }

            const auto& seed = seeds.front();
            const std::string head_key = federation::circle_head_key(circle_id);

            federation::NodusCommandResult head_get;
            try {
                head_get = federation::nodus_cli_get(config, seed, head_key);
            } catch (const std::exception& e) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "nodus_get_failed"},
                    {"message", e.what()}
                });
                return;
            }

            const std::string event_id = extract_nodus_value(head_get.output);
            if (head_get.exit_code != 0 || event_id.empty()) {
                set_json(res, 200, {
                    {"ok", false},
                    {"error", "head_not_found"},
                    {"head_key", head_key},
                    {"head_get", command_result_json(head_get)}
                });
                return;
            }

            const std::string event_key = federation::circle_event_key(circle_id, event_id);

            federation::NodusCommandResult event_get;
            try {
                event_get = federation::nodus_cli_get(config, seed, event_key);
            } catch (const std::exception& e) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "nodus_event_get_failed"},
                    {"message", e.what()}
                });
                return;
            }

            const std::string event_json_raw = extract_nodus_value(event_get.output);
            if (event_get.exit_code != 0 || event_json_raw.empty()) {
                set_json(res, 200, {
                    {"ok", false},
                    {"error", "event_not_found"},
                    {"event_key", event_key},
                    {"event_get", command_result_json(event_get)}
                });
                return;
            }

            json event;
            try {
                event = json::parse(event_json_raw);
            } catch (...) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_event_json"},
                    {"event_key", event_key},
                    {"raw", event_json_raw}
                });
                return;
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
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_event_fields"},
                    {"expected_circle_id", circle_id},
                    {"expected_event_id", event_id},
                    {"event", event}
                });
                return;
            }

            std::string store_err;
            if (!federation::store_circle_federation_inbox_event(
                    parsed_circle_id,
                    parsed_event_id,
                    event_type,
                    origin_nas,
                    created_epoch,
                    event_key,
                    event_json_raw,
                    &store_err)) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "inbox_store_failed"},
                    {"message", store_err}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"stored", true},
                {"circle_id", parsed_circle_id},
                {"event_id", parsed_event_id},
                {"event_type", event_type},
                {"origin_nas", origin_nas},
                {"head_key", head_key},
                {"event_key", event_key},
                {"head_get", command_result_json(head_get)},
                {"event_get", command_result_json(event_get)},
                {"event", event}
            });
        });

    server.Get("/api/v4/admin/nodus/outbox/stats",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            std::string err;
            const auto stats = federation::circle_federation_outbox_stats(&err);
            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "outbox_error"},
                    {"message", err}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"total", stats.total},
                {"pending", stats.pending},
                {"publishing", stats.publishing},
                {"done", stats.done},
                {"failed", stats.failed},
                {"retry_wait", stats.retry_wait}
            });
        });

    server.Get("/api/v4/admin/nodus/outbox/list",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            const int limit = query_int(req, "limit", 50, 1, 500);

            std::string err;
            const auto rows = federation::list_circle_federation_outbox(limit, &err);
            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "outbox_error"},
                    {"message", err}
                });
                return;
            }

            json events = json::array();
            for (const auto& row : rows) {
                events.push_back(outbox_event_json(row));
            }

            set_json(res, 200, {
                {"ok", true},
                {"count", events.size()},
                {"events", events}
            });
        });

    server.Post("/api/v4/admin/nodus/outbox/enqueue-ping",
        [deps](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            if (!require_admin(deps, req, res, &actor_fp)) return;

            json body;
            if (!parse_json_body(req, res, &body)) return;

            std::string circle_id = json_value_to_string(body, "circle_id");
            if (circle_id.empty()) circle_id = "research-circle";

            std::string event_id = json_value_to_string(body, "event_id");
            if (event_id.empty()) event_id = make_event_id();

            std::string origin_nas = json_value_to_string(body, "origin_nas");
            if (origin_nas.empty()) origin_nas = actor_fp.empty() ? "local-pqnas" : actor_fp;

            std::string message = json_value_to_string(body, "message");
            if (message.empty()) {
                message = "Circle Stack queued Nodus research ping from PQ-NAS";
            }

            federation::CircleEventDraft draft;
            draft.type = "circle.ping";
            draft.event_id = event_id;
            draft.circle_id = circle_id;
            draft.origin_nas = origin_nas;
            draft.created_at_iso = now_iso_utc();
            draft.message = message;

            std::string event_json;
            std::string event_key;
            std::string head_key;

            try {
                event_json = federation::make_circle_ping_event_json(draft);
                event_key = federation::circle_event_key(circle_id, event_id);
                head_key = federation::circle_head_key(circle_id);
            } catch (const std::exception& e) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_event"},
                    {"message", e.what()}
                });
                return;
            }

            std::string err;
            if (!federation::enqueue_circle_federation_event(
                    draft.type,
                    circle_id,
                    event_id,
                    event_key,
                    head_key,
                    event_json,
                    &err)) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "outbox_enqueue_failed"},
                    {"message", err}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"queued", true},
                {"circle_id", circle_id},
                {"event_id", event_id},
                {"event_key", event_key},
                {"head_key", head_key},
                {"event", json::parse(event_json)}
            });
        });



    server.Post("/api/v4/admin/nodus/outbox/recover-leases",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            std::string err;
            const int recovered =
                federation::recover_stale_circle_federation_outbox_leases(&err);

            if (!err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "outbox_recover_failed"},
                    {"message", err}
                });
                return;
            }

            set_json(res, 200, {
                {"ok", true},
                {"recovered", recovered}
            });
        });

    server.Post("/api/v4/admin/nodus/outbox/drain-once",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            json body;
            if (!parse_json_body(req, res, &body)) return;

            int limit = 10;
            if (body.contains("limit") && body["limit"].is_number_integer()) {
                limit = std::clamp(body["limit"].get<int>(), 1, 50);
            }

            int lease_seconds = 300;
            if (body.contains("lease_seconds") && body["lease_seconds"].is_number_integer()) {
                lease_seconds = std::clamp(body["lease_seconds"].get<int>(), 10, 3600);
            }

            const int max_attempts = body.contains("max_attempts") && body["max_attempts"].is_number_integer()
                ? std::clamp(body["max_attempts"].get<int>(), 1, 20)
                : 5;

            const std::string seed_selector =
                json_value_to_string(body, "seed").empty()
                    ? "EU-1"
                    : json_value_to_string(body, "seed");

            std::string seed_err;
            const auto seeds = select_seeds(seed_selector, false, &seed_err);
            if (seeds.empty()) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_seed"},
                    {"message", seed_err.empty() ? "no Nodus seeds configured" : seed_err}
                });
                return;
            }

            const auto config = make_nodus_config();
            std::string identity_err;
            if (!ensure_identity_dir(config, &identity_err)) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "identity_dir_error"},
                    {"message", identity_err}
                });
                return;
            }

            std::string claim_err;
            const auto rows = federation::claim_circle_federation_outbox_pending(
                limit,
                lease_seconds,
                &claim_err);

            if (!claim_err.empty()) {
                set_json(res, 500, {
                    {"ok", false},
                    {"error", "outbox_claim_failed"},
                    {"message", claim_err}
                });
                return;
            }

            json events = json::array();
            int done_count = 0;
            int retry_count = 0;
            int failed_count = 0;

            for (const auto& row : rows) {
                json event_result = outbox_event_json(row);
                event_result["publish"] = json::array();

                bool all_ok = true;

                for (const auto& seed : seeds) {
                    json item = seed_json(seed);

                    try {
                        const auto event_put =
                            federation::nodus_cli_put(config, seed, row.event_key, row.event_json);

                        const int recent_slot = static_cast<int>(row.id % 64);
                        const std::string recent_key =
                            federation::circle_recent_key(row.circle_id, recent_slot);

                        item["put_event"] = command_result_json(event_put);
                        item["recent_key"] = recent_key;

                        if (event_put.exit_code != 0) {
                            item["put_head"] = {
                                {"ok", false},
                                {"exit_code", -1},
                                {"output", "skipped because event PUT failed"}
                            };
                            item["put_recent"] = {
                                {"ok", false},
                                {"exit_code", -1},
                                {"output", "skipped because event PUT failed"}
                            };
                            all_ok = false;
                        } else {
                            const auto head_put =
                                federation::nodus_cli_put(config, seed, row.head_key, row.event_id);
                            const auto recent_put =
                                federation::nodus_cli_put(config, seed, recent_key, row.event_id);

                            const std::string recent_index_key =
                                federation::circle_recent_index_key(row.circle_id);
                            const std::string recent_index_json =
                                recent_index_json_for_outbox_event(row, 20);
                            const auto recent_index_put =
                                federation::nodus_cli_put(config, seed, recent_index_key, recent_index_json);

                            item["put_head"] = command_result_json(head_put);
                            item["put_recent"] = command_result_json(recent_put);
                            item["put_recent_index"] = command_result_json(recent_index_put);
                            item["recent_index_key"] = recent_index_key;

                            all_ok = all_ok &&
                                     head_put.exit_code == 0 &&
                                     recent_put.exit_code == 0 &&
                                     recent_index_put.exit_code == 0;
                        }
                    } catch (const std::exception& e) {
                        item["put_event"] = {
                            {"ok", false},
                            {"exit_code", -1},
                            {"output", e.what()}
                        };
                        item["put_head"] = {
                            {"ok", false},
                            {"exit_code", -1},
                            {"output", "skipped"}
                        };
                        item["put_recent"] = {
                            {"ok", false},
                            {"exit_code", -1},
                            {"output", "skipped"}
                        };
                        all_ok = false;
                    }

                    event_result["publish"].push_back(item);
                }

                std::string mark_err;

                if (all_ok) {
                    if (federation::mark_circle_federation_outbox_done(row.id, &mark_err)) {
                        event_result["final_status"] = "done";
                        ++done_count;
                    } else {
                        event_result["final_status"] = "mark_done_failed";
                        event_result["mark_error"] = mark_err;
                        ++failed_count;
                    }
                } else if (row.attempts >= max_attempts) {
                    const std::string publish_err =
                        outbox_publish_error_summary(event_result["publish"].front());

                    if (federation::mark_circle_federation_outbox_failed(row.id, publish_err, &mark_err)) {
                        event_result["final_status"] = "failed";
                        ++failed_count;
                    } else {
                        event_result["final_status"] = "mark_failed_failed";
                        event_result["mark_error"] = mark_err;
                        ++failed_count;
                    }
                } else {
                    const int delay = outbox_retry_delay_seconds(row.attempts);
                    const std::string publish_err =
                        outbox_publish_error_summary(event_result["publish"].front());

                    if (federation::mark_circle_federation_outbox_retry(
                            row.id,
                            publish_err,
                            delay,
                            &mark_err)) {
                        event_result["final_status"] = "pending";
                        event_result["retry_delay_seconds"] = delay;
                        ++retry_count;
                    } else {
                        event_result["final_status"] = "mark_retry_failed";
                        event_result["mark_error"] = mark_err;
                        ++failed_count;
                    }
                }

                events.push_back(event_result);
            }

            set_json(res, 200, {
                {"ok", failed_count == 0},
                {"claimed", rows.size()},
                {"done", done_count},
                {"retry", retry_count},
                {"failed", failed_count},
                {"events", events}
            });
        });

    server.Post("/api/v4/admin/nodus/put-test",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            json body;
            if (!parse_json_body(req, res, &body)) return;

            std::string key = json_value_to_string(body, "key");
            if (key.empty()) key = "pqnas:research:nodus:put-test:" + make_event_id();

            if (!valid_research_key(key)) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_key"},
                    {"message", "key must start with pqnas:"}
                });
                return;
            }

            std::string value = json_value_to_string(body, "value");
            if (value.empty()) {
                value = json{
                    {"type", "pqnas.nodus.put_test"},
                    {"created_at", now_iso_utc()},
                    {"message", "hello from PQ-NAS"}
                }.dump();
            }

            std::string seed_err;
            const auto seeds = select_seeds(json_value_to_string(body, "seed"), false, &seed_err);
            if (seeds.empty()) {
                set_json(res, 400, {{"ok", false}, {"error", "invalid_seed"}, {"message", seed_err}});
                return;
            }

            const auto config = make_nodus_config();
            std::string identity_err;
            if (!ensure_identity_dir(config, &identity_err)) {
                set_json(res, 500, {{"ok", false}, {"error", "identity_dir_error"}, {"message", identity_err}});
                return;
            }

            json results = json::array();
            bool all_ok = true;

            for (const auto& seed : seeds) {
                json item = seed_json(seed);

                try {
                    const auto r = federation::nodus_cli_put(config, seed, key, value);
                    item["put"] = command_result_json(r);
                    all_ok = all_ok && r.exit_code == 0;
                } catch (const std::exception& e) {
                    item["put"] = {{"ok", false}, {"exit_code", -1}, {"output", e.what()}};
                    all_ok = false;
                }

                results.push_back(item);
            }

            set_json(res, 200, {
                {"ok", all_ok},
                {"key", key},
                {"value", value},
                {"results", results}
            });
        });

    server.Get("/api/v4/admin/nodus/get-test",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            const std::string key = req.has_param("key") ? req.get_param_value("key") : "";
            if (!valid_research_key(key)) {
                set_json(res, 400, {
                    {"ok", false},
                    {"error", "invalid_key"},
                    {"message", "query parameter key is required and must start with pqnas:"}
                });
                return;
            }

            std::string seed_err;
            const std::string selector = req.has_param("seed") ? req.get_param_value("seed") : "all";
            const auto seeds = select_seeds(selector, true, &seed_err);
            if (seeds.empty()) {
                set_json(res, 400, {{"ok", false}, {"error", "invalid_seed"}, {"message", seed_err}});
                return;
            }

            const auto config = make_nodus_config();
            std::string identity_err;
            if (!ensure_identity_dir(config, &identity_err)) {
                set_json(res, 500, {{"ok", false}, {"error", "identity_dir_error"}, {"message", identity_err}});
                return;
            }

            json results = json::array();
            bool any_ok = false;

            for (const auto& seed : seeds) {
                json item = seed_json(seed);

                try {
                    const auto r = federation::nodus_cli_get(config, seed, key);
                    item["get"] = command_result_json(r);
                    any_ok = any_ok || r.exit_code == 0;
                } catch (const std::exception& e) {
                    item["get"] = {{"ok", false}, {"exit_code", -1}, {"output", e.what()}};
                }

                results.push_back(item);
            }

            set_json(res, 200, {
                {"ok", any_ok},
                {"key", key},
                {"results", results}
            });
        });

    server.Post("/api/v4/admin/nodus/circle/ping",
        [deps](const httplib::Request& req, httplib::Response& res) {
            std::string actor_fp;
            if (!require_admin(deps, req, res, &actor_fp)) return;

            json body;
            if (!parse_json_body(req, res, &body)) return;

            std::string circle_id = json_value_to_string(body, "circle_id");
            if (circle_id.empty()) circle_id = "research-circle";

            std::string event_id = json_value_to_string(body, "event_id");
            if (event_id.empty()) event_id = make_event_id();

            std::string origin_nas = json_value_to_string(body, "origin_nas");
            if (origin_nas.empty()) origin_nas = actor_fp.empty() ? "local-pqnas" : actor_fp;

            std::string message = json_value_to_string(body, "message");
            if (message.empty()) message = "Circle Stack Nodus research ping from PQ-NAS";

            federation::CircleEventDraft draft;
            draft.type = "circle.ping";
            draft.event_id = event_id;
            draft.circle_id = circle_id;
            draft.origin_nas = origin_nas;
            draft.created_at_iso = now_iso_utc();
            draft.message = message;

            std::string event_json;
            std::string event_key;
            std::string head_key;

            try {
                event_json = federation::make_circle_ping_event_json(draft);
                event_key = federation::circle_event_key(circle_id, event_id);
                head_key = federation::circle_head_key(circle_id);
            } catch (const std::exception& e) {
                set_json(res, 400, {{"ok", false}, {"error", "invalid_event"}, {"message", e.what()}});
                return;
            }

            std::string seed_err;
            const auto seeds = select_seeds(json_value_to_string(body, "seed"), true, &seed_err);
            if (seeds.empty()) {
                set_json(res, 400, {{"ok", false}, {"error", "invalid_seed"}, {"message", seed_err}});
                return;
            }

            const auto config = make_nodus_config();
            std::string identity_err;
            if (!ensure_identity_dir(config, &identity_err)) {
                set_json(res, 500, {{"ok", false}, {"error", "identity_dir_error"}, {"message", identity_err}});
                return;
            }

            json results = json::array();
            bool all_ok = true;

            for (const auto& seed : seeds) {
                json item = seed_json(seed);

                try {
                    const auto event_put =
                        federation::nodus_cli_put(config, seed, event_key, event_json);
                    const auto head_put =
                        federation::nodus_cli_put(config, seed, head_key, event_id);

                    item["put_event"] = command_result_json(event_put);
                    item["put_head"] = command_result_json(head_put);

                    all_ok = all_ok && event_put.exit_code == 0 && head_put.exit_code == 0;
                } catch (const std::exception& e) {
                    item["put_event"] = {{"ok", false}, {"exit_code", -1}, {"output", e.what()}};
                    item["put_head"] = {{"ok", false}, {"exit_code", -1}, {"output", "skipped"}};
                    all_ok = false;
                }

                results.push_back(item);
            }

            set_json(res, 200, {
                {"ok", all_ok},
                {"circle_id", circle_id},
                {"event_id", event_id},
                {"event_key", event_key},
                {"head_key", head_key},
                {"event", json::parse(event_json)},
                {"results", results}
            });
        });
}

} // namespace pqnas
