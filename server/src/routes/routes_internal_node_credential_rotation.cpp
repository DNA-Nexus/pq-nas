#include "routes/routes_internal_node_credential_rotation.h"

#include "cluster/internal_auth.h"
#include "cluster/storage_node_credential_runtime.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kMaximumAuthorizationHeaderBytes = 128;

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_internal_auth_token_text(
            &value_
        );
    }

    SensitiveStringWiper(
        const SensitiveStringWiper&
    ) = delete;

    SensitiveStringWiper& operator=(
        const SensitiveStringWiper&
    ) = delete;

private:
    std::string& value_;
};

void clear_json_credential(
    json* object
) noexcept {
    if (!object ||
        !object->is_object()) {
        return;
    }

    try {
        auto iterator =
            object->find(
                "node_credential"
            );

        if (iterator != object->end() &&
            iterator->is_string()) {
            std::string& value =
                iterator->get_ref<
                    std::string&
                >();

            clear_internal_auth_token_text(
                &value
            );
        }

        object->erase(
            "node_credential"
        );
    } catch (...) {
        // Security: best-effort wipe only. Response construction has already
        // completed and no credential value may be logged from this path.
    }
}

class JsonCredentialWiper {
public:
    explicit JsonCredentialWiper(
        json& value
    ) noexcept
        : value_(value) {}

    ~JsonCredentialWiper() {
        clear_json_credential(
            &value_
        );
    }

    JsonCredentialWiper(
        const JsonCredentialWiper&
    ) = delete;

    JsonCredentialWiper& operator=(
        const JsonCredentialWiper&
    ) = delete;

private:
    json& value_;
};

class RotationRateLimiter {
public:
    bool allow(
        const std::string& remote_address,
        long* retry_after_seconds
    ) {
        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        const std::string peer =
            remote_address.empty()
                ? "<unknown>"
                : (
                    remote_address.size() <= 128
                        ? remote_address
                        : "<oversized>"
                );

        const auto now =
            Clock::now();

        std::lock_guard<std::mutex>
            lock(mutex_);

        prune_expired(
            now
        );

        auto found =
            windows_.find(
                peer
            );

        if (found ==
            windows_.end()) {
            if (windows_.size() >=
                kInternalNodeCredentialRotationRateLimitTrackedPeers) {
                if (retry_after_seconds) {
                    *retry_after_seconds =
                        kInternalNodeCredentialRotationRateLimitWindowSeconds;
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
                std::chrono::
                    duration_cast<
                        std::chrono::seconds
                    >(
                        now -
                        window.started_at
                    ).count()
            );

        if (elapsed_seconds >=
            kInternalNodeCredentialRotationRateLimitWindowSeconds) {
            window.started_at =
                now;

            window.requests = 1;

            return true;
        }

        if (window.requests >=
            kInternalNodeCredentialRotationRateLimitRequests) {
            if (retry_after_seconds) {
                *retry_after_seconds =
                    std::max(
                        1L,
                        kInternalNodeCredentialRotationRateLimitWindowSeconds -
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
                std::chrono::
                    duration_cast<
                        std::chrono::seconds
                    >(
                        now -
                        iterator->second.started_at
                    ).count();

            if (elapsed >=
                kInternalNodeCredentialRotationRateLimitWindowSeconds) {
                iterator =
                    windows_.erase(
                        iterator
                    );

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

RotationRateLimiter
    g_rotation_rate_limiter;

bool context_ok(
    const InternalNodeCredentialRotationRoutesContext&
        context
) noexcept {
    return
        context.runtime != nullptr;
}

void set_security_headers(
    httplib::Response& response
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
}

void reply_json(
    httplib::Response& response,
    int status,
    const json& body
) {
    set_security_headers(
        response
    );

    response.status =
        status;

    response.set_content(
        body.dump(),
        "application/json; charset=utf-8"
    );
}

void reply_unauthorized(
    httplib::Response& response
) {
    response.set_header(
        "WWW-Authenticate",
        "Bearer"
    );

    // Security: deliberately do not disclose whether the credential was
    // missing, malformed, stale OLD, unexpected NEW, or otherwise invalid.
    reply_json(
        response,
        401,
        json{
            {"ok", false},
            {"error", "unauthorized"}
        }
    );
}

void reply_unavailable(
    httplib::Response& response
) {
    // Security: filesystem, credential generation, persistence and rotation
    // state failures are intentionally collapsed at the HTTP boundary.
    reply_json(
        response,
        503,
        json{
            {"ok", false},
            {"error", "rotation_unavailable"}
        }
    );
}

const std::string*
exact_authorization_header(
    const httplib::Request& request
) noexcept {
    const auto range =
        request.headers.equal_range(
            "Authorization"
        );

    auto iterator =
        range.first;

    if (iterator ==
        range.second) {
        return nullptr;
    }

    const std::string* value =
        &iterator->second;

    ++iterator;

    // Security: reject duplicate Authorization fields so different HTTP
    // components cannot authenticate different bearer values.
    if (iterator !=
            range.second ||
        value->empty() ||
        value->size() >
            kMaximumAuthorizationHeaderBytes) {
        return nullptr;
    }

    // Avoid another plaintext bearer copy.
    return value;
}

bool preauthenticate(
    const InternalNodeCredentialRotationRoutesContext& context,
    const std::string& authorization
) noexcept {
    try {
        return
            context.runtime &&
            context.runtime->
                verify_authorization(
                    authorization
                );
    } catch (...) {
        // Security: credential dependency failures fail closed.
        return false;
    }
}

bool consume_rate_limit(
    const httplib::Request& request,
    httplib::Response& response
) {
    long retry_after_seconds = 0;

    bool allowed = false;

    try {
        allowed =
            g_rotation_rate_limiter.allow(
                request.remote_addr,
                &retry_after_seconds
            );
    } catch (...) {
        reply_unavailable(
            response
        );

        return false;
    }

    if (allowed) {
        return true;
    }

    response.set_header(
        "Retry-After",
        std::to_string(
            std::max(
                1L,
                retry_after_seconds
            )
        )
    );

    reply_json(
        response,
        429,
        json{
            {"ok", false},
            {"error", "rate_limited"}
        }
    );

    return false;
}

bool request_preamble(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalNodeCredentialRotationRoutesContext& context,
    const std::string** authorization
) {
    set_security_headers(
        response
    );

    if (!authorization) {
        reply_unavailable(
            response
        );

        return false;
    }

    *authorization =
        nullptr;

    if (!context_ok(
            context)) {
        reply_unavailable(
            response
        );

        return false;
    }

    const std::string* presented =
        exact_authorization_header(
            request
        );

    if (!presented ||
        !preauthenticate(
            context,
            *presented)) {
        reply_unauthorized(
            response
        );

        return false;
    }

    // Security: unauthenticated traffic does not consume the mutation budget.
    // The runtime performs authorization again atomically with the mutation,
    // so this preliminary check does not create an auth/mutation TOCTOU.
    if (!consume_rate_limit(
            request,
            response)) {
        return false;
    }

    if (!request.body.empty()) {
        reply_json(
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"}
            }
        );

        return false;
    }

    *authorization =
        presented;

    return true;
}

void reply_prepare_success(
    httplib::Response& response,
    const std::string& pending_token
) {
    json body{
        {"ok", true},
        {"node_credential", pending_token}
    };

    JsonCredentialWiper
        json_wiper(body);

    std::string serialized =
        body.dump();

    SensitiveStringWiper
        serialized_wiper(
            serialized
        );

    set_security_headers(
        response
    );

    response.status = 200;

    // httplib must retain one response-body copy until transmission. All
    // additional local plaintext copies are wiped immediately after this call.
    response.set_content(
        serialized,
        "application/json; charset=utf-8"
    );
}

} // namespace

void
handle_internal_node_credential_rotation_prepare_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalNodeCredentialRotationRoutesContext&
        context
) {
    const std::string* authorization =
        nullptr;

    if (!request_preamble(
            request,
            response,
            context,
            &authorization)) {
        return;
    }

    std::string pending_token;

    SensitiveStringWiper
        pending_token_wiper(
            pending_token
        );

    std::string internal_error;

    StorageNodeCredentialRuntimeResult result =
        StorageNodeCredentialRuntimeResult::
            PersistFailed;

    try {
        result =
            context.runtime->
                prepare_rotation_authorized(
                    *authorization,
                    &pending_token,
                    &internal_error
                );
    } catch (...) {
        internal_error.clear();

        reply_unavailable(
            response
        );

        return;
    }

    // Security: internal persistence details are not part of the wire
    // protocol and must never be reflected to the caller.
    internal_error.clear();

    if (result ==
        StorageNodeCredentialRuntimeResult::
            AuthenticationFailed) {
        reply_unauthorized(
            response
        );

        return;
    }

    if (result !=
        StorageNodeCredentialRuntimeResult::
            Ok) {
        reply_unavailable(
            response
        );

        return;
    }

    reply_prepare_success(
        response,
        pending_token
    );
}

void
handle_internal_node_credential_rotation_finalize_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalNodeCredentialRotationRoutesContext&
        context
) {
    const std::string* authorization =
        nullptr;

    if (!request_preamble(
            request,
            response,
            context,
            &authorization)) {
        return;
    }

    std::string internal_error;

    StorageNodeCredentialRuntimeResult result =
        StorageNodeCredentialRuntimeResult::
            PersistFailed;

    try {
        result =
            context.runtime->
                finalize_rotation_authorized(
                    *authorization,
                    &internal_error
                );
    } catch (...) {
        internal_error.clear();

        reply_unavailable(
            response
        );

        return;
    }

    internal_error.clear();

    if (result ==
        StorageNodeCredentialRuntimeResult::
            AuthenticationFailed) {
        reply_unauthorized(
            response
        );

        return;
    }

    // NoPending is the idempotent recovery result after a successful finalize
    // response was lost. The currently active credential has already been
    // authenticated by finalize_rotation_authorized().
    if (result ==
            StorageNodeCredentialRuntimeResult::
                Ok ||
        result ==
            StorageNodeCredentialRuntimeResult::
                NoPending) {
        reply_json(
            response,
            200,
            json{
                {"ok", true}
            }
        );

        return;
    }

    reply_unavailable(
        response
    );
}

void
register_internal_node_credential_rotation_routes(
    httplib::Server& server,
    const InternalNodeCredentialRotationRoutesContext&
        context
) {
    const InternalNodeCredentialRotationRoutesContext
        captured =
            context;

    server.Post(
        "/internal/node/credential/rotation/prepare",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_internal_node_credential_rotation_prepare_request(
                request,
                response,
                captured
            );
        }
    );

    server.Post(
        "/internal/node/credential/rotation/finalize",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_internal_node_credential_rotation_finalize_request(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
