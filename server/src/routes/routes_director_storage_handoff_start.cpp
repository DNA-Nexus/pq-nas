#include "routes/routes_director_storage_handoff_start.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/storage_handoff_ticket_store.h"
#include "storage_metadata_validation.h"
#include "cluster/strict_json_parse.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;


void wipe_string(std::string* value) noexcept {
    if (!value) {
        return;
    }

    std::fill(
        value->begin(),
        value->end(),
        '\0'
    );

    value->clear();
}


class StringWiper {
public:
    explicit StringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~StringWiper() {
        wipe_string(&value_);
    }

    StringWiper(const StringWiper&) = delete;
    StringWiper& operator=(const StringWiper&) = delete;

private:
    std::string& value_;
};


bool header_name_equal_ci(
    const std::string& left,
    const char* right
) noexcept {
    if (!right) {
        return false;
    }

    const std::string expected(right);

    if (left.size() != expected.size()) {
        return false;
    }

    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto a =
            static_cast<unsigned char>(left[i]);

        const auto b =
            static_cast<unsigned char>(expected[i]);

        if (std::tolower(a) !=
            std::tolower(b)) {
            return false;
        }
    }

    return true;
}


// Security: duplicate security-sensitive headers are rejected rather than
// relying on httplib/intermediary selection order.
const std::string* exact_single_header(
    const httplib::Request& request,
    const char* name
) noexcept {
    const std::string* found = nullptr;

    for (const auto& header : request.headers) {
        if (!header_name_equal_ci(
                header.first,
                name)) {
            continue;
        }

        if (found) {
            return nullptr;
        }

        found = &header.second;
    }

    return found;
}


bool has_header_ci(
    const httplib::Request& request,
    const char* name
) noexcept {
    for (const auto& header : request.headers) {
        if (header_name_equal_ci(
                header.first,
                name)) {
            return true;
        }
    }

    return false;
}


void reply_json_local(
    httplib::Response& response,
    int status,
    const json& body
) {
    response.status = status;
    response.set_content(
        body.dump(),
        "application/json; charset=utf-8"
    );
}


bool exact_json_content_type(
    const httplib::Request& request
) noexcept {
    const std::string* content_type =
        exact_single_header(
            request,
            "Content-Type"
        );

    return
        content_type &&
        *content_type == "application/json";
}


bool normalize_expected_origin(
    const std::string& configured,
    std::string* normalized
) {
    if (!normalized) {
        return false;
    }

    normalized->clear();

    if (!normalize_cluster_https_base_url(
            configured,
            normalized,
            nullptr)) {
        return false;
    }

    // Security: trusted browser authority must already be canonical.
    // Do not silently reinterpret configuration at this boundary.
    return *normalized == configured;
}


bool valid_target(
    const DirectorStorageHandoffTarget& target
) {
    if (!valid_cluster_node_id(
            target.home_node) ||
        target.projection_revision == 0 ||
        target.api_base_url.empty() ||
        target.browser_base_url.empty()) {
        return false;
    }

    std::string normalized_api;

    if (!normalize_cluster_https_base_url(
            target.api_base_url,
            &normalized_api,
            nullptr) ||
        normalized_api !=
            target.api_base_url) {
        return false;
    }

    std::string normalized_browser;

    if (!normalize_cluster_https_base_url(
            target.browser_base_url,
            &normalized_browser,
            nullptr) ||
        normalized_browser !=
            target.browser_base_url) {
        return false;
    }

    return true;
}


bool same_target(
    const DirectorStorageHandoffTarget& left,
    const DirectorStorageHandoffTarget& right
) noexcept {
    return
        left.home_node ==
            right.home_node &&
        left.projection_revision ==
            right.projection_revision &&
        left.api_base_url ==
            right.api_base_url &&
        left.browser_base_url ==
            right.browser_base_url;
}


bool context_ok(
    const DirectorStorageHandoffStartRoutesContext& context,
    std::string* normalized_origin
) {
    return
        normalize_expected_origin(
            context.expected_director_origin,
            normalized_origin
        ) &&
        context.require_user_cookie &&
        context.consume_rate_limit_slot &&
        context.resolve_authoritative_target &&
        context.issue_ticket;
}


void reply_restart_required(
    httplib::Response& response
) {
    reply_json_local(
        response,
        409,
        json{
            {"ok", false},
            {
                "error",
                "handoff_restart_required"
            }
        }
    );
}


class DefaultRateLimiterState {
public:
    bool consume(
        const std::string& fingerprint,
        long* retry_after_seconds
    ) {
        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        if (fingerprint.empty()) {
            return false;
        }

        using Clock =
            std::chrono::steady_clock;

        const auto now = Clock::now();

        const auto window =
            std::chrono::seconds(
                kDirectorStorageHandoffStartRateLimitWindowSeconds
            );

        const auto cutoff =
            now - window;

        std::lock_guard<std::mutex>
            lock(mutex_);

        // Keep the tracked-user map bounded even when many different valid
        // accounts touch the endpoint over time.
        for (auto it = hits_.begin();
             it != hits_.end();) {
            auto& values = it->second;

            while (!values.empty() &&
                   values.front() <= cutoff) {
                values.pop_front();
            }

            if (values.empty()) {
                it = hits_.erase(it);
            } else {
                ++it;
            }
        }

        auto existing =
            hits_.find(fingerprint);

        if (existing == hits_.end() &&
            hits_.size() >=
                kDirectorStorageHandoffStartRateLimitTrackedUsers) {
            if (retry_after_seconds) {
                *retry_after_seconds = 1;
            }

            return false;
        }

        auto& values =
            hits_[fingerprint];

        while (!values.empty() &&
               values.front() <= cutoff) {
            values.pop_front();
        }

        if (values.size() >=
            kDirectorStorageHandoffStartRateLimitRequests) {
            if (retry_after_seconds) {
                const auto remaining =
                    std::chrono::duration_cast<
                        std::chrono::seconds
                    >(
                        window -
                        (now - values.front())
                    ).count();

                *retry_after_seconds =
                    std::max<long>(
                        1,
                        static_cast<long>(
                            remaining
                        )
                    );
            }

            return false;
        }

        values.push_back(now);
        return true;
    }

private:
    std::mutex mutex_;

    std::unordered_map<
        std::string,
        std::deque<
            std::chrono::steady_clock::time_point
        >
    > hits_;
};


} // namespace


std::function<bool(
    const std::string& fingerprint,
    long* retry_after_seconds
)>
make_default_director_storage_handoff_start_rate_limiter() {
    auto state =
        std::make_shared<
            DefaultRateLimiterState
        >();

    return
        [state](
            const std::string& fingerprint,
            long* retry_after_seconds
        ) {
            return state->consume(
                fingerprint,
                retry_after_seconds
            );
        };
}


void handle_director_storage_handoff_start_request(
    const httplib::Request& request,
    httplib::Response& response,
    const DirectorStorageHandoffStartRoutesContext& context
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    response.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );

    std::string expected_origin;

    if (!context_ok(
            context,
            &expected_origin)) {
        reply_json_local(
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"}
            }
        );

        return;
    }

    // Browser handoff is deliberately cookie-only. Merely supplying a Bearer
    // header must never select another authentication mode.
    if (has_header_ci(
            request,
            "Authorization")) {
        reply_json_local(
            response,
            403,
            json{
                {"ok", false},
                {"error", "forbidden"}
            }
        );

        return;
    }

    const std::string* origin =
        exact_single_header(
            request,
            "Origin"
        );

    // Security: require one exact Director Origin. No Referer fallback,
    // Host trust or X-Forwarded-* derivation exists on this capability route.
    if (!origin ||
        *origin != expected_origin) {
        reply_json_local(
            response,
            403,
            json{
                {"ok", false},
                {"error", "forbidden"}
            }
        );

        return;
    }

    std::string actor_fingerprint;
    std::string actor_role;

    bool authenticated = false;

    try {
        authenticated =
            context.require_user_cookie(
                request,
                response,
                &actor_fingerprint,
                &actor_role
            );
    } catch (...) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    if (!authenticated) {
        // Authentication helper owns its precise 401/403 response.
        return;
    }

    if (!pqnas::valid_storage_user_fingerprint_hex(
            actor_fingerprint)) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    long retry_after_seconds = 0;
    bool within_rate_limit = false;

    try {
        within_rate_limit =
            context.consume_rate_limit_slot(
                actor_fingerprint,
                &retry_after_seconds
            );
    } catch (...) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    if (!within_rate_limit) {
        response.set_header(
            "Retry-After",
            std::to_string(
                std::max(
                    1L,
                    retry_after_seconds
                )
            )
        );

        reply_json_local(
            response,
            429,
            json{
                {"ok", false},
                {"error", "rate_limited"}
            }
        );

        return;
    }

    if (!exact_json_content_type(
            request)) {
        reply_json_local(
            response,
            415,
            json{
                {"ok", false},
                {
                    "error",
                    "unsupported_media_type"
                }
            }
        );

        return;
    }

    if (request.body.empty() ||
        request.body.size() >
            kDirectorStorageHandoffStartMaxRequestBytes) {
        reply_json_local(
            response,
            request.body.empty()
                ? 400
                : 413,
            json{
                {"ok", false},
                {
                    "error",
                    request.body.empty()
                        ? "bad_request"
                        : "request_too_large"
                }
            }
        );

        return;
    }

    json body;

    if (!parse_json_reject_duplicate_keys(
            request.body,
            &body) ||
        !body.is_object() ||
        body.size() != 1 ||
        body.find(
            "browser_binding_hash") ==
            body.end() ||
        !body[
            "browser_binding_hash"
        ].is_string()) {
        reply_json_local(
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    std::string browser_binding_hash;

    try {
        browser_binding_hash =
            body[
                "browser_binding_hash"
            ].get<std::string>();
    } catch (...) {
        reply_json_local(
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    if (!valid_storage_handoff_browser_binding_hash(
            browser_binding_hash)) {
        reply_json_local(
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    DirectorStorageHandoffTargetResult
        before;

    try {
        // Security: this is the last Director authorization read before the
        // external ticket-minting side effect.
        before =
            context.resolve_authoritative_target(
                actor_fingerprint
            );
    } catch (...) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    if (before.code ==
        DirectorStorageHandoffTargetCode::
            UserNotAuthorized) {
        reply_json_local(
            response,
            403,
            json{
                {"ok", false},
                {"error", "user_not_authorized"}
            }
        );

        return;
    }

    if (before.code !=
            DirectorStorageHandoffTargetCode::Ok ||
        !valid_target(
            before.target)) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    StorageHandoffTicketClientResult
        issue_result;

    try {
        issue_result =
            context.issue_ticket(
                before.target.home_node,
                actor_fingerprint,
                browser_binding_hash
            );
    } catch (...) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    StringWiper
        ticket_wiper(
            issue_result.ticket
        );

    if (issue_result.code ==
        StorageHandoffTicketClientCode::
            RestartRequired) {
        reply_restart_required(response);
        return;
    }

    if (issue_result.code ==
        StorageHandoffTicketClientCode::
            RateLimited) {
        reply_json_local(
            response,
            429,
            json{
                {"ok", false},
                {"error", "rate_limited"}
            }
        );

        return;
    }

    if (issue_result.code ==
        StorageHandoffTicketClientCode::
            UserNotAuthorized) {
        reply_json_local(
            response,
            403,
            json{
                {"ok", false},
                {"error", "user_not_authorized"}
            }
        );

        return;
    }

    if (issue_result.code !=
            StorageHandoffTicketClientCode::Ok ||
        !valid_storage_handoff_ticket_text(
            issue_result.ticket) ||
        issue_result.expires_at <= 0) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    DirectorStorageHandoffTargetResult
        after;

    try {
        // A ticket now exists. Re-read every Director-owned authority used to
        // choose the browser destination. If anything changed, never reveal
        // the ticket; the browser must restart with a fresh preauth binding.
        after =
            context.resolve_authoritative_target(
                actor_fingerprint
            );
    } catch (...) {
        reply_restart_required(response);
        return;
    }

    if (after.code !=
            DirectorStorageHandoffTargetCode::Ok ||
        !valid_target(
            after.target) ||
        !same_target(
            before.target,
            after.target)) {
        reply_restart_required(response);
        return;
    }

    // Ticket is canonical base64url and therefore requires no JSON escaping.
    // browser_base_url is escaped independently. Avoid placing T into a JSON
    // DOM so the only application-owned plaintext copies are the client result
    // and this short-lived serialized response buffer.
    const std::string browser_origin_json =
        json(
            after.target.browser_base_url
        ).dump();

    std::string success_body =
        std::string(
            "{\"ok\":true,"
            "\"storage_origin\":"
        ) +
        browser_origin_json +
        ",\"ticket\":\"" +
        issue_result.ticket +
        "\",\"expires_at\":" +
        std::to_string(
            issue_result.expires_at
        ) +
        "}";

    StringWiper
        response_body_wiper(
            success_body
        );

    response.status = 200;
    response.set_content(
        success_body,
        "application/json; charset=utf-8"
    );
}


void register_director_storage_handoff_start_routes(
    httplib::Server& server,
    const DirectorStorageHandoffStartRoutesContext& context
) {
    server.Post(
        kDirectorStorageHandoffStartPath,
        [context](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_director_storage_handoff_start_request(
                request,
                response,
                context
            );
        }
    );
}

} // namespace pqnas::cluster
