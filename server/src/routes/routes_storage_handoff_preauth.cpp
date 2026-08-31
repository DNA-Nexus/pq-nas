#include "routes/routes_storage_handoff_preauth.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/storage_handoff_ticket_store.h"
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

class DefaultStorageHandoffPreauthRateLimiter {
public:
    bool allow(
        const std::string& remote_address,
        long* retry_after_seconds
    ) {
        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        // Security: use the transport peer rather than X-Forwarded-For or
        // another client-controlled forwarding header.
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
                kStorageHandoffPreauthRateLimitTrackedPeers) {
                if (retry_after_seconds) {
                    *retry_after_seconds =
                        kStorageHandoffPreauthRateLimitWindowSeconds;
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
            kStorageHandoffPreauthRateLimitWindowSeconds) {
            window.started_at = now;
            window.requests = 1;
            return true;
        }

        if (window.requests >=
            kStorageHandoffPreauthRateLimitRequests) {
            if (retry_after_seconds) {
                *retry_after_seconds =
                    std::max(
                        1L,
                        kStorageHandoffPreauthRateLimitWindowSeconds -
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
                kStorageHandoffPreauthRateLimitWindowSeconds) {
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

class BrowserBindingSecretWiper {
public:
    explicit BrowserBindingSecretWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~BrowserBindingSecretWiper() {
        // Security: the plaintext binding secret belongs only in the
        // short-lived browser cookie.
        clear_storage_handoff_browser_binding_secret_text(
            &value_
        );
    }

    BrowserBindingSecretWiper(
        const BrowserBindingSecretWiper&
    ) = delete;

    BrowserBindingSecretWiper& operator=(
        const BrowserBindingSecretWiper&
    ) = delete;

private:
    std::string& value_;
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

const std::string* exact_origin_header(
    const httplib::Request& request
) noexcept {
    const auto range =
        request.headers.equal_range(
            "Origin"
        );

    auto iterator =
        range.first;

    if (iterator == range.second) {
        return nullptr;
    }

    const std::string* value =
        &iterator->second;

    ++iterator;

    // Security: duplicated Origin headers are ambiguous across proxies and
    // application parsers, so reject rather than choosing one.
    if (iterator != range.second ||
        value->empty() ||
        value->size() >
            kMaximumOriginHeaderBytes) {
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
    // be canonical. Do not silently transform the trusted authority here.
    if (normalized != configured) {
        return false;
    }

    *output = std::move(normalized);
    return true;
}

} // namespace

void handle_storage_handoff_preauth_request(
    const httplib::Request& request,
    httplib::Response& response,
    const StorageHandoffPreauthRoutesContext& context
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
    std::string director_browser_origin;

    try {
        if (!canonical_expected_origin(
                context.expected_origin,
                &expected_origin) ||
            !canonical_expected_origin(
                context.director_browser_origin,
                &director_browser_origin) ||
            !context.consume_rate_limit_slot) {
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
        exact_origin_header(
            request
        );

    // Security: this browser bootstrap never accepts a Bearer bypass and never
    // derives the trusted origin from Host/X-Forwarded-* request headers.
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

    // Security: this endpoint accepts no attacker-controlled payload at all.
    if (request.body.size() >
        kStorageHandoffPreauthMaxRequestBytes) {
        reply_json_local(
            response,
            413,
            json{
                {"ok", false},
                {"error", "request_too_large"}
            }
        );

        return;
    }

    if (!request.body.empty()) {
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
        // disabling protection on an unauthenticated session endpoint.
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "preauth_unavailable"}
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

    std::string browser_binding_secret;

    BrowserBindingSecretWiper
        secret_wiper(
            browser_binding_secret
        );

    if (!generate_storage_handoff_browser_binding_secret(
            &browser_binding_secret)) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "preauth_unavailable"}
            }
        );

        return;
    }

    std::string browser_binding_hash;

    if (!storage_handoff_browser_binding_hash(
            browser_binding_secret,
            &browser_binding_hash)) {
        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "preauth_unavailable"}
            }
        );

        return;
    }

    // Security: H may travel through the Director. B never appears in JSON,
    // logs, URLs, or Director-visible state.
    //
    // Build the complete successful response before attaching B. This keeps
    // cookie issuance fail-closed if response serialization/allocation fails.
    reply_json_local(
        response,
        200,
        json{
            {"ok", true},
            {
                "browser_binding_hash",
                browser_binding_hash
            },
            {
                "director_origin",
                director_browser_origin
            }
        }
    );

    try {
        // Security:
        // - HttpOnly keeps B away from JavaScript.
        // - Secure requires HTTPS.
        // - SameSite=Strict prevents cross-site cookie attachment.
        // - narrow Path exposes B only to the later Storage consume endpoint.
        //
        // Set this header last so a partially constructed success response
        // cannot mint a usable browser binding cookie.
        response.set_header(
            "Set-Cookie",
            std::string(
                kStorageHandoffPreauthCookieName
            ) +
            "=" +
            browser_binding_secret +
            "; Path=" +
            kStorageHandoffConsumePath +
            "; Max-Age=" +
            std::to_string(
                kStorageHandoffPreauthTtlSeconds
            ) +
            "; HttpOnly"
            "; SameSite=Strict"
            "; Secure"
        );
    } catch (...) {
        response.headers.erase(
            "Set-Cookie"
        );

        reply_json_local(
            response,
            503,
            json{
                {"ok", false},
                {"error", "preauth_unavailable"}
            }
        );

        return;
    }
}

void register_storage_handoff_preauth_routes(
    httplib::Server& server,
    const StorageHandoffPreauthRoutesContext& context
) {
    StorageHandoffPreauthRoutesContext
        captured = context;

    if (!captured.consume_rate_limit_slot) {
        const auto limiter =
            std::make_shared<
                DefaultStorageHandoffPreauthRateLimiter
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
        "/api/storage/handoff/preauth",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_storage_handoff_preauth_request(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
