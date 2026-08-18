#include "routes/routes_internal_storage_handoff.h"

#include "cluster/internal_auth.h"
#include "cluster/storage_handoff_ticket_store.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kMaximumAuthorizationHeaderBytes = 128;

class DefaultStorageHandoffRateLimiter {
public:
    bool allow(
        const std::string& remote_address,
        long* retry_after_seconds
    ) {
        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        // Security: remote_addr comes from the transport, not a forwarded
        // client-controlled header. Empty transport identities share one
        // conservative bucket instead of bypassing rate limiting.
        const std::string peer =
            remote_address.empty()
                ? "<unknown>"
                : remote_address;

        const auto now =
            Clock::now();

        std::lock_guard<std::mutex>
            lock(mutex_);

        prune_expired(now);

        auto found =
            windows_.find(peer);

        if (found == windows_.end()) {
            // Security: bound limiter bookkeeping so unexpected peer
            // identities cannot grow process memory without limit.
            if (windows_.size() >=
                kInternalStorageHandoffRateLimitTrackedPeers) {
                if (retry_after_seconds) {
                    *retry_after_seconds =
                        kInternalStorageHandoffRateLimitWindowSeconds;
                }

                return false;
            }

            windows_.emplace(
                peer,
                PeerWindow{
                    now,
                    1
                }
            );

            return true;
        }

        PeerWindow& window =
            found->second;

        const long elapsed_seconds =
            static_cast<long>(
                std::chrono::duration_cast<
                    std::chrono::seconds
                >(
                    now - window.started_at
                ).count()
            );

        if (elapsed_seconds >=
            kInternalStorageHandoffRateLimitWindowSeconds) {
            window.started_at = now;
            window.requests = 1;
            return true;
        }

        if (window.requests >=
            kInternalStorageHandoffRateLimitRequests) {
            if (retry_after_seconds) {
                *retry_after_seconds =
                    std::max(
                        1L,
                        kInternalStorageHandoffRateLimitWindowSeconds -
                            elapsed_seconds
                    );
            }

            return false;
        }

        ++window.requests;
        return true;
    }

private:
    using Clock =
        std::chrono::steady_clock;

    struct PeerWindow {
        Clock::time_point started_at;
        std::size_t requests = 0;
    };

    void prune_expired(
        const Clock::time_point now
    ) {
        auto iterator =
            windows_.begin();

        while (iterator !=
               windows_.end()) {
            const auto elapsed =
                std::chrono::duration_cast<
                    std::chrono::seconds
                >(
                    now -
                    iterator->second.started_at
                ).count();

            if (elapsed >=
                kInternalStorageHandoffRateLimitWindowSeconds) {
                iterator =
                    windows_.erase(iterator);
                continue;
            }

            ++iterator;
        }
    }

    std::mutex mutex_;

    std::unordered_map<
        std::string,
        PeerWindow
    > windows_;
};

class TicketWiper {
public:
    explicit TicketWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~TicketWiper() {
        // Security: plaintext handoff tickets are short-lived bearer material.
        clear_storage_handoff_ticket_text(
            &value_
        );
    }

    TicketWiper(
        const TicketWiper&
    ) = delete;

    TicketWiper& operator=(
        const TicketWiper&
    ) = delete;

private:
    std::string& value_;
};

bool context_ok(
    const InternalStorageHandoffRoutesContext& context
) noexcept {
    return
        context.credential &&
        context.credential->configured() &&
        context.ticket_store &&
        static_cast<bool>(
            context.consume_rate_limit_slot) &&
        static_cast<bool>(
            context.authorize_user);
}

void reply_json_local(
    const InternalStorageHandoffRoutesContext& context,
    httplib::Response& response,
    int status,
    const json& body
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    response.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );

    const std::string text =
        body.dump();

    if (context.reply_json) {
        context.reply_json(
            response,
            status,
            text
        );

        return;
    }

    response.status = status;

    response.set_content(
        text,
        "application/json; charset=utf-8"
    );
}

void reply_unauthorized(
    const InternalStorageHandoffRoutesContext& context,
    httplib::Response& response
) {
    response.set_header(
        "WWW-Authenticate",
        "Bearer"
    );

    // Security: do not reveal whether the header was missing, malformed,
    // duplicated, or contained the wrong node credential.
    reply_json_local(
        context,
        response,
        401,
        json{
            {"ok", false},
            {"error", "unauthorized"}
        }
    );
}

const std::string* exact_authorization_header(
    const httplib::Request& request
) noexcept {
    const auto range =
        request.headers.equal_range(
            "Authorization"
        );

    auto iterator = range.first;

    if (iterator == range.second) {
        return nullptr;
    }

    const std::string* value =
        &iterator->second;

    ++iterator;

    // Security: reject duplicated Authorization headers so intermediaries and
    // the application cannot authenticate different bearer values.
    if (iterator != range.second ||
        value->empty() ||
        value->size() >
            kMaximumAuthorizationHeaderBytes) {
        return nullptr;
    }

    // Security: return a view of request-owned storage. Do not create another
    // plaintext copy of the service-to-service bearer credential.
    return value;
}

std::string trim_ascii(
    const std::string& value
) {
    std::size_t begin = 0;

    while (begin < value.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   value[begin]
               ))) {
        ++begin;
    }

    std::size_t end =
        value.size();

    while (end > begin &&
           std::isspace(
               static_cast<unsigned char>(
                   value[end - 1]
               ))) {
        --end;
    }

    return value.substr(
        begin,
        end - begin
    );
}

std::string lowercase_ascii(
    std::string value
) {
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character)
            );
        }
    );

    return value;
}

bool exact_json_content_type(
    const httplib::Request& request
) {
    const auto range =
        request.headers.equal_range(
            "Content-Type"
        );

    auto iterator = range.first;

    if (iterator == range.second) {
        return false;
    }

    std::string value =
        iterator->second;

    ++iterator;

    if (iterator != range.second ||
        value.empty() ||
        value.size() > 128) {
        return false;
    }

    value =
        lowercase_ascii(
            trim_ascii(value)
        );

    const std::size_t separator =
        value.find(';');

    const std::string media_type =
        trim_ascii(
            value.substr(
                0,
                separator
            )
        );

    if (media_type !=
        "application/json") {
        return false;
    }

    if (separator ==
        std::string::npos) {
        return true;
    }

    const std::string parameter =
        trim_ascii(
            value.substr(
                separator + 1
            )
        );

    return parameter ==
        "charset=utf-8";
}

bool valid_fingerprint(
    const std::string& fingerprint
) noexcept {
    if (fingerprint.size() !=
        kStorageHandoffFingerprintHexChars) {
        return false;
    }

    for (unsigned char character :
         fingerprint) {
        const bool hex =
            (character >= '0' &&
             character <= '9') ||
            (character >= 'a' &&
             character <= 'f') ||
            (character >= 'A' &&
             character <= 'F');

        if (!hex) {
            return false;
        }
    }

    return true;
}

} // namespace

void handle_internal_storage_handoff_ticket_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalStorageHandoffRoutesContext& context
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    response.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );

    if (!context_ok(context)) {
        reply_json_local(
            context,
            response,
            500,
            json{
                {"ok", false},
                {"error", "server_error"}
            }
        );

        return;
    }

    const std::string* authorization =
        exact_authorization_header(
            request
        );

    if (!authorization ||
        !verify_internal_bearer_authorization(
            *authorization,
            *context.credential)) {
        reply_unauthorized(
            context,
            response
        );

        return;
    }

    long retry_after_seconds = 0;
    bool within_rate_limit = false;

    try {
        within_rate_limit =
            context.consume_rate_limit_slot(
                request.remote_addr,
                &retry_after_seconds
            );
    } catch (...) {
        // Security: limiter failure must fail closed rather than silently
        // disabling request throttling.
        reply_json_local(
            context,
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
            context,
            response,
            429,
            json{
                {"ok", false},
                {"error", "rate_limited"}
            }
        );

        return;
    }

    // Security: reject ambiguous or unexpectedly encoded bodies before JSON
    // parsing so intermediaries and the route agree on the request format.
    if (!exact_json_content_type(request)) {
        reply_json_local(
            context,
            response,
            415,
            json{
                {"ok", false},
                {"error", "unsupported_media_type"}
            }
        );

        return;
    }

    if (request.body.empty() ||
        request.body.size() >
            kInternalStorageHandoffMaxRequestBytes) {
        reply_json_local(
            context,
            response,
            request.body.empty()
                ? 400
                : 413,
            json{
                {"ok", false},
                {"error",
                    request.body.empty()
                        ? "bad_request"
                        : "request_too_large"}
            }
        );

        return;
    }

    json body;

    try {
        body = json::parse(
            request.body
        );
    } catch (...) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    if (!body.is_object() ||
        body.size() != 2 ||
        body.find("fingerprint") ==
            body.end() ||
        !body["fingerprint"].is_string() ||
        body.find("browser_binding_hash") ==
            body.end() ||
        !body["browser_binding_hash"].is_string()) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    std::string fingerprint;
    std::string browser_binding_hash;

    try {
        fingerprint =
            body["fingerprint"].
                get<std::string>();

        browser_binding_hash =
            body["browser_binding_hash"].
                get<std::string>();
    } catch (...) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    if (!valid_fingerprint(
            fingerprint) ||
        !valid_storage_handoff_browser_binding_hash(
            browser_binding_hash)) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    bool user_authorized = false;
    std::string authorized_home_node;
    std::uint64_t authorized_projection_revision = 0;

    try {
        // Security: authorization returns the exact local projection snapshot
        // that justified issuance. The one-time ticket carries this binding so
        // browser redemption can reject a placement/revision transition that
        // happened after the ticket was created.
        user_authorized =
            context.authorize_user(
                fingerprint,
                &authorized_home_node,
                &authorized_projection_revision
            );
    } catch (...) {
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    if (!user_authorized) {
        reply_json_local(
            context,
            response,
            403,
            json{
                {"ok", false},
                {"error", "user_not_authorized"}
            }
        );

        return;
    }

    std::string ticket;
    TicketWiper ticket_wiper(ticket);

    std::int64_t expires_at = 0;

    const StorageHandoffTicketResult result =
        context.ticket_store->issue(
            fingerprint,
            authorized_home_node,
            authorized_projection_revision,
            browser_binding_hash,
            kInternalStorageHandoffTicketTtlSeconds,
            &ticket,
            &expires_at
        );

    if (result !=
        StorageHandoffTicketResult::Ok) {
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {"error", "handoff_unavailable"}
            }
        );

        return;
    }

    reply_json_local(
        context,
        response,
        200,
        json{
            {"ok", true},
            {"ticket", ticket},
            {"expires_at", expires_at}
        }
    );
}

void register_internal_storage_handoff_routes(
    httplib::Server& server,
    const InternalStorageHandoffRoutesContext& context
) {
    InternalStorageHandoffRoutesContext
        captured = context;

    if (!captured.consume_rate_limit_slot) {
        const auto limiter =
            std::make_shared<
                DefaultStorageHandoffRateLimiter
            >();

        captured.consume_rate_limit_slot =
            [limiter](
                const std::string& remote_address,
                long* retry_after_seconds
            ) {
                return limiter->allow(
                    remote_address,
                    retry_after_seconds
                );
            };
    }

    server.Post(
        "/internal/storage/handoff/ticket",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
