#include "routes_circle_nodus_research.h"

#include "federation/circle_federation_event.h"
#include "federation/circle_federation_outbox.h"
#include "federation/pqnas_nodus_client.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

federation::NodusClientConfig make_nodus_config() {
    federation::NodusClientConfig config;

    if (const char* p = std::getenv("PQNAS_NODUS_CLI")) {
        if (p[0]) config.nodus_cli_path = p;
    }

    config.identity_dir = "/srv/pqnas/config/nodus/research_identity";
    if (const char* p = std::getenv("PQNAS_NODUS_IDENTITY_DIR")) {
        if (p[0]) config.identity_dir = p;
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
    server.Get("/api/v4/admin/nodus/status",
        [deps](const httplib::Request& req, httplib::Response& res) {
            if (!require_admin(deps, req, res, nullptr)) return;

            const int timeout_ms = query_int(req, "timeout_ms", 1500, 100, 10000);
            const auto config = make_nodus_config();

            json seeds = json::array();
            for (const auto& seed : federation::default_nodus_seeds()) {
                std::string err;
                const bool reachable =
                    tcp_check(seed.host, seed.client_port, timeout_ms, &err);

                json item = seed_json(seed);
                item["reachable"] = reachable;
                item["error"] = reachable ? "" : err;
                seeds.push_back(item);
            }

            set_json(res, 200, {
                {"ok", true},
                {"nodus_cli_path", config.nodus_cli_path},
                {"identity_dir", config.identity_dir},
                {"timeout_seconds", config.timeout_seconds},
                {"timeout_ms", timeout_ms},
                {"seeds", seeds}
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
                        const auto head_put =
                            federation::nodus_cli_put(config, seed, row.head_key, row.event_id);

                        item["put_event"] = command_result_json(event_put);
                        item["put_head"] = command_result_json(head_put);

                        all_ok = all_ok &&
                                 event_put.exit_code == 0 &&
                                 head_put.exit_code == 0;
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
