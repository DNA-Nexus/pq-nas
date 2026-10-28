#include "routes/routes_storage_handoff_consume.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/storage_handoff_ticket_store.h"
#include "cluster/storage_user_session_cookie.h"
#include "routes/routes_storage_handoff_preauth.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kMaximumOriginHeaderBytes = 2048;

constexpr const char*
    kFormContentType =
        "application/x-www-form-urlencoded";

class DefaultStorageHandoffConsumeRateLimiter {
public:
    bool allow(
        const std::string& remote_address,
        long* retry_after_seconds
    ) {
        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        // Security: trust only the transport peer here. Forwarding headers are
        // client-controlled unless a separately authenticated proxy boundary
        // explicitly establishes otherwise.
        const std::string peer =
            remote_address.empty()
                ? "<unknown>"
                : remote_address;

        const auto now = Clock::now();

        std::lock_guard<std::mutex>
            lock(mutex_);

        prune_expired(now);

        auto found =
            windows_.find(peer);

        if (found == windows_.end()) {
            // Security: bound attacker-controlled limiter bookkeeping.
            if (windows_.size() >=
                kStorageHandoffConsumeRateLimitTrackedPeers) {
                if (retry_after_seconds) {
                    *retry_after_seconds =
                        kStorageHandoffConsumeRateLimitWindowSeconds;
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
                    now -
                    window.started_at
                ).count()
            );

        if (elapsed_seconds >=
            kStorageHandoffConsumeRateLimitWindowSeconds) {
            window.started_at = now;
            window.requests = 1;

            return true;
        }

        if (window.requests >=
            kStorageHandoffConsumeRateLimitRequests) {
            if (retry_after_seconds) {
                *retry_after_seconds =
                    std::max(
                        1L,
                        kStorageHandoffConsumeRateLimitWindowSeconds -
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
                kStorageHandoffConsumeRateLimitWindowSeconds) {
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

class SensitiveTextWiper {
public:
    using ClearFunction =
        void (*)(std::string*) noexcept;

    SensitiveTextWiper(
        std::string* value,
        ClearFunction clear_function
    ) noexcept
        : value_(value),
          clear_function_(clear_function) {}

    ~SensitiveTextWiper() {
        if (value_ && clear_function_) {
            clear_function_(value_);
        }
    }

    SensitiveTextWiper(
        const SensitiveTextWiper&
    ) = delete;

    SensitiveTextWiper& operator=(
        const SensitiveTextWiper&
    ) = delete;

private:
    std::string* value_;
    ClearFunction clear_function_;
};

void reply_json_local(
    httplib::Response& response,
    int status,
    const json& body
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    response.set_header(
        "Pragma",
        "no-cache"
    );

    response.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );

    response.set_header(
        "Referrer-Policy",
        "no-referrer"
    );

    response.status = status;

    response.set_content(
        body.dump(),
        "application/json; charset=utf-8"
    );
}

const std::string* exact_single_header(
    const httplib::Request& request,
    const char* name,
    std::size_t maximum_bytes
) noexcept {
    const auto range =
        request.headers.equal_range(
            name
        );

    auto iterator =
        range.first;

    if (iterator == range.second) {
        return nullptr;
    }

    const std::string* value =
        &iterator->second;

    ++iterator;

    // Security: duplicate security-sensitive headers are ambiguous across
    // proxies and parsers. Reject instead of selecting one.
    if (iterator != range.second ||
        value->empty() ||
        value->size() > maximum_bytes) {
        return nullptr;
    }

    return value;
}

bool canonical_expected_origin(
    const std::string& configured,
    std::string* output
) {
    if (!output) {
        return false;
    }

    output->clear();

    std::string normalized;
    std::string error;

    if (!normalize_cluster_https_base_url(
            configured,
            &normalized,
            &error)) {
        return false;
    }

    // Security: configuration used as an authorization boundary must already
    // be canonical. Do not silently repair it at request time.
    if (normalized != configured) {
        return false;
    }

    *output = std::move(
        normalized
    );

    return true;
}

bool canonical_b64url_43(
    const std::string& value
) noexcept {
    if (value.size() !=
        kStorageHandoffTicketChars) {
        return false;
    }

    for (const char character : value) {
        const bool valid =
            (character >= 'A' &&
             character <= 'Z') ||
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9') ||
            character == '-' ||
            character == '_';

        if (!valid) {
            return false;
        }
    }

    return true;
}

bool parse_ticket_form(
    const std::string& body,
    std::string* ticket
) {
    if (!ticket) {
        return false;
    }

    clear_storage_handoff_ticket_text(
        ticket
    );

    constexpr const char
        prefix[] = "ticket=";

    constexpr std::size_t
        prefix_size = sizeof(prefix) - 1;

    if (body.size() !=
            prefix_size +
                kStorageHandoffTicketChars ||
        body.compare(
            0,
            prefix_size,
            prefix
        ) != 0) {
        return false;
    }

    std::string candidate =
        body.substr(
            prefix_size
        );

    if (!canonical_b64url_43(
            candidate)) {
        clear_storage_handoff_ticket_text(
            &candidate
        );

        return false;
    }

    ticket->swap(
        candidate
    );

    clear_storage_handoff_ticket_text(
        &candidate
    );

    return true;
}

bool extract_single_preauth_cookie(
    const std::string& cookie_header,
    std::string* output
) {
    if (!output) {
        return false;
    }

    clear_storage_handoff_browser_binding_secret_text(
        output
    );

    bool found = false;

    std::size_t position = 0;

    while (position <
           cookie_header.size()) {
        while (position <
                   cookie_header.size() &&
               (cookie_header[position] == ' ' ||
                cookie_header[position] == '\t' ||
                cookie_header[position] == ';')) {
            ++position;
        }

        if (position >=
            cookie_header.size()) {
            break;
        }

        const std::size_t end =
            cookie_header.find(
                ';',
                position
            );

        const std::size_t segment_end =
            end == std::string::npos
                ? cookie_header.size()
                : end;

        const std::size_t equals =
            cookie_header.find(
                '=',
                position
            );

        if (equals ==
                std::string::npos ||
            equals >=
                segment_end ||
            equals == position) {
            return false;
        }

        const std::string name =
            cookie_header.substr(
                position,
                equals - position
            );

        const std::string value =
            cookie_header.substr(
                equals + 1,
                segment_end -
                    (equals + 1)
            );

        if (name ==
            kStorageHandoffPreauthCookieName) {
            // Security: cookie tossing / duplicated cookie names must not
            // allow the application and proxy/browser to disagree about B.
            if (found ||
                !canonical_b64url_43(
                    value)) {
                clear_storage_handoff_browser_binding_secret_text(
                    output
                );

                return false;
            }

            *output = value;
            found = true;
        }

        if (end ==
            std::string::npos) {
            break;
        }

        position =
            end + 1;
    }

    return found;
}

bool valid_set_cookie_value(
    const std::string& value
) noexcept {
    if (value.empty()) {
        return false;
    }

    for (const unsigned char character : value) {
        // Security: reject controls, whitespace and cookie/header separators.
        // This prevents response splitting if a future service callback
        // violates the Storage cookie primitive's output contract.
        if (character <= 0x20 ||
            character >= 0x7f ||
            character == '"' ||
            character == ',' ||
            character == ';' ||
            character == '\\') {
            return false;
        }
    }

    return true;
}

void clear_set_cookie_headers(
    httplib::Response& response
) noexcept {
    response.headers.erase(
        "Set-Cookie"
    );
}

} // namespace

void handle_storage_handoff_consume_request(
    const httplib::Request& request,
    httplib::Response& response,
    const StorageHandoffConsumeRoutesContext& context
) {
    std::string expected_origin;

    try {
        if (!canonical_expected_origin(
                context.expected_storage_origin,
                &expected_origin) ||
            !context.consume_rate_limit_slot ||
            !context.consume ||
            !context.now_epoch) {
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
    } catch (...) {
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

    const std::string* origin =
        exact_single_header(
            request,
            "Origin",
            kMaximumOriginHeaderBytes
        );

    // Security: ticket redemption is same-origin with the configured
    // Storage authority. This keeps the HttpOnly SameSite=Strict browser
    // binding usable without weakening it for cross-site attachment.
    // No Bearer or Host/X-Forwarded-* bypass exists here.
    if (!origin ||
        *origin != expected_origin) {
        reply_json_local(
            response,
            403,
            json{
                {"ok", false},
                {"error", "handoff_rejected"}
            }
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

    const std::string* content_type =
        exact_single_header(
            request,
            "Content-Type",
            128
        );

    if (!content_type ||
        *content_type !=
            kFormContentType) {
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

    if (request.body.empty() ||
        request.body.size() >
            kStorageHandoffConsumeMaxRequestBytes) {
        reply_json_local(
            response,
            request.body.size() >
                    kStorageHandoffConsumeMaxRequestBytes
                ? 413
                : 400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return;
    }

    std::string ticket;

    SensitiveTextWiper
        ticket_wiper(
            &ticket,
            clear_storage_handoff_ticket_text
        );

    if (!parse_ticket_form(
            request.body,
            &ticket)) {
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

    const std::string* cookie_header =
        exact_single_header(
            request,
            "Cookie",
            kStorageHandoffConsumeMaxCookieHeaderBytes
        );

    std::string browser_binding_secret;

    SensitiveTextWiper
        browser_binding_wiper(
            &browser_binding_secret,
            clear_storage_handoff_browser_binding_secret_text
        );

    if (!cookie_header ||
        !extract_single_preauth_cookie(
            *cookie_header,
            &browser_binding_secret)) {
        reply_json_local(
            response,
            403,
            json{
                {"ok", false},
                {"error", "handoff_rejected"}
            }
        );

        return;
    }

    StorageHandoffConsumeServiceOutput
        output;

    SensitiveTextWiper
        session_cookie_wiper(
            &output.session_cookie_value,
            clear_storage_user_session_cookie_text
        );

    StorageHandoffConsumeServiceResult
        service_result =
            StorageHandoffConsumeServiceResult::
                InternalError;

    try {
        service_result =
            context.consume(
                ticket,
                browser_binding_secret,
                &output
            );
    } catch (...) {
        service_result =
            StorageHandoffConsumeServiceResult::
                InternalError;
    }

    if (service_result !=
        StorageHandoffConsumeServiceResult::Ok) {
        // Security: malformed, missing, expired, wrong-binding, stale
        // projection and other dynamic authorization failures are intentionally
        // indistinguishable to the browser.
        if (service_result ==
                StorageHandoffConsumeServiceResult::
                    Rejected ||
            service_result ==
                StorageHandoffConsumeServiceResult::
                    InvalidInput) {
            reply_json_local(
                response,
                403,
                json{
                    {"ok", false},
                    {"error", "handoff_rejected"}
                }
            );
        } else {
            reply_json_local(
                response,
                503,
                json{
                    {"ok", false},
                    {"error", "handoff_unavailable"}
                }
            );
        }

        return;
    }

    long now = 0;

    try {
        now =
            context.now_epoch();
    } catch (...) {
        now = 0;
    }

    if (!valid_set_cookie_value(
            output.session_cookie_value) ||
        output.session_cookie_value.size() >
            kStorageUserSessionCookieMaxBytes ||
        now <= 0 ||
        output.expires_at <= now ||
        output.expires_at - now >
            kStorageUserSessionCookieMaxTtlSeconds) {
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

    const long max_age =
        output.expires_at -
        now;

    // Construct the full success response before attaching either cookie.
    reply_json_local(
        response,
        200,
        json{
            {"ok", true}
        }
    );

    try {
        const std::string session_cookie =
            std::string(
                kStorageUserSessionCookieName
            ) +
            "=" +
            output.session_cookie_value +
            "; Path=/" +
            "; Max-Age=" +
            std::to_string(
                max_age
            ) +
            "; HttpOnly" +
            "; SameSite=Strict" +
            "; Secure";

        const std::string clear_preauth_cookie =
            std::string(
                kStorageHandoffPreauthCookieName
            ) +
            "=" +
            "; Path=" +
            kStorageHandoffConsumePath +
            "; Max-Age=0" +
            "; HttpOnly" +
            "; SameSite=Strict" +
            "; Secure";

        // Security: both state changes are delivered in the same response.
        // httplib uses a multimap, so preserve both Set-Cookie fields.
        response.headers.emplace(
            "Set-Cookie",
            session_cookie
        );

        response.headers.emplace(
            "Set-Cookie",
            clear_preauth_cookie
        );
    } catch (...) {
        clear_set_cookie_headers(
            response
        );

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
}

void register_storage_handoff_consume_routes(
    httplib::Server& server,
    const StorageHandoffConsumeRoutesContext& context
) {
    StorageHandoffConsumeRoutesContext
        captured = context;

    if (!captured.consume_rate_limit_slot) {
        const auto limiter =
            std::make_shared<
                DefaultStorageHandoffConsumeRateLimiter
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
        kStorageHandoffConsumePath,
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_storage_handoff_consume_request(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
