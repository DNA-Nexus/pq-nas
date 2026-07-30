#include "routes_internal_node_status.h"

#include "cluster/internal_auth.h"
#include "httplib.h"

#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t kRateBucketCap = 4096;
constexpr std::size_t kMaxAuthorizationHeaderBytes = 128;

std::mutex g_rate_mutex;
std::unordered_map<
    std::string,
    std::deque<std::int64_t>
> g_rate_buckets;

std::int64_t now_epoch_fallback() {
    return static_cast<std::int64_t>(std::time(nullptr));
}

std::int64_t now_epoch(
    const InternalNodeStatusRoutesContext& context
) {
    if (context.now_epoch_sec) {
        return context.now_epoch_sec();
    }

    return now_epoch_fallback();
}

void reply_json_route(
    const InternalNodeStatusRoutesContext& context,
    httplib::Response& res,
    int status,
    const json& body
) {
    if (context.reply_json) {
        context.reply_json(res, status, body.dump());
        return;
    }

    res.status = status;
    res.set_content(
        body.dump(),
        "application/json; charset=utf-8"
    );
}

bool context_ok(
    const InternalNodeStatusRoutesContext& context
) {
    return
        context.credential &&
        context.credential->configured() &&
        static_cast<bool>(context.build_status_json);
}

bool exact_authorization_header(
    const httplib::Request& req,
    std::string* out
) {
    if (!out) {
        return false;
    }

    out->clear();

    const auto range =
        req.headers.equal_range("Authorization");

    auto iterator = range.first;

    if (iterator == range.second) {
        return false;
    }

    const std::string value = iterator->second;
    ++iterator;

    // Security: reject duplicated Authorization headers instead of allowing
    // intermediaries and the application to interpret different values.
    if (iterator != range.second) {
        return false;
    }

    if (value.empty() ||
        value.size() > kMaxAuthorizationHeaderBytes) {
        return false;
    }

    *out = value;
    return true;
}

std::string rate_key(const httplib::Request& req) {
    if (req.remote_addr.empty()) {
        return "unknown";
    }

    // Security: remote_addr is used only as an opaque in-memory map key.
    // It is never reflected in responses or written to logs here.
    if (req.remote_addr.size() > 128) {
        return "oversized";
    }

    return req.remote_addr;
}

void compact_rate_buckets_locked(std::int64_t now) {
    const std::int64_t minimum =
        now - kInternalNodeStatusRateWindowSeconds;

    for (auto iterator = g_rate_buckets.begin();
         iterator != g_rate_buckets.end();) {
        auto& hits = iterator->second;

        while (!hits.empty() &&
               hits.front() < minimum) {
            hits.pop_front();
        }

        if (hits.empty()) {
            iterator = g_rate_buckets.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

bool rate_limited(
    const httplib::Request& req,
    std::int64_t now
) {
    const std::string key = rate_key(req);
    const std::int64_t minimum =
        now - kInternalNodeStatusRateWindowSeconds;

    std::lock_guard<std::mutex> lock(g_rate_mutex);

    if (g_rate_buckets.size() >= kRateBucketCap) {
        compact_rate_buckets_locked(now);
    }

    auto found = g_rate_buckets.find(key);

    // Security: keep the attacker-controlled key map bounded.
    if (g_rate_buckets.size() >= kRateBucketCap &&
        found == g_rate_buckets.end()) {
        return true;
    }

    auto& hits = g_rate_buckets[key];

    while (!hits.empty() &&
           hits.front() < minimum) {
        hits.pop_front();
    }

    if (static_cast<int>(hits.size()) >=
        kInternalNodeStatusRateLimit) {
        return true;
    }

    hits.push_back(now);
    return false;
}

void reply_unauthorized(
    const InternalNodeStatusRoutesContext& context,
    httplib::Response& res
) {
    res.set_header(
        "WWW-Authenticate",
        "Bearer"
    );

    reply_json_route(
        context,
        res,
        401,
        json{
            {"ok", false},
            {"error", "unauthorized"},
            {"message", "authentication required"}
        }
    );
}

} // namespace

void handle_internal_node_status_request(
    const httplib::Request& req,
    httplib::Response& res,
    const InternalNodeStatusRoutesContext& context
) {
    res.set_header("Cache-Control", "no-store");

    if (!context_ok(context)) {
        reply_json_route(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "internal route unavailable"}
            }
        );
        return;
    }

    if (rate_limited(req, now_epoch(context))) {
        res.set_header("Retry-After", "60");

        reply_json_route(
            context,
            res,
            429,
            json{
                {"ok", false},
                {"error", "rate_limited"},
                {"message", "request limit exceeded"}
            }
        );
        return;
    }

    std::string authorization;

    if (!exact_authorization_header(
            req,
            &authorization) ||
        !verify_internal_bearer_authorization(
            authorization,
            *context.credential)) {
        reply_unauthorized(context, res);
        return;
    }

    // Security: host, storage, and user metadata are collected only after
    // successful service-to-service authentication.
    json status_json;
    std::string status_error;

    if (!context.build_status_json(
            &status_json,
            &status_error)) {
        // Do not expose local paths, errno values, or collection details.
        reply_json_route(
            context,
            res,
            503,
            json{
                {"ok", false},
                {"error", "node_status_unavailable"},
                {"message", "node status unavailable"}
            }
        );
        return;
    }

    reply_json_route(
        context,
        res,
        200,
        status_json
    );
}

void register_internal_node_status_routes(
    httplib::Server& server,
    const InternalNodeStatusRoutesContext& context
) {
    const InternalNodeStatusRoutesContext captured = context;

    server.Get(
        "/internal/node/status",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_internal_node_status_request(
                req,
                res,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
