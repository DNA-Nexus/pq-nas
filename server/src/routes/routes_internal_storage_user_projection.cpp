#include "routes/routes_internal_storage_user_projection.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/storage_user_projection_audit.h"
#include "cluster/storage_user_projection_store.h"
#include "httplib.h"
#include "storage_metadata_validation.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kMaximumAuthorizationHeaderBytes = 128;

class DefaultStorageUserProjectionRateLimiter {
public:
    bool allow(
        const std::string& remote_address,
        long* retry_after_seconds
    ) {
        if (retry_after_seconds) {
            *retry_after_seconds = 0;
        }

        // Security: remote_addr is transport-owned. Empty identities share a
        // conservative bucket instead of bypassing throttling.
        const std::string peer =
            remote_address.empty()
                ? "<unknown>"
                : remote_address;

        const auto now =
            Clock::now();

        std::lock_guard<std::mutex> lock(
            mutex_
        );

        prune_expired(now);

        auto found =
            windows_.find(peer);

        if (found == windows_.end()) {
            // Security: bound attacker-influenced limiter bookkeeping.
            if (windows_.size() >=
                kInternalStorageUserProjectionRateLimitTrackedPeers) {
                if (retry_after_seconds) {
                    *retry_after_seconds =
                        kInternalStorageUserProjectionRateLimitWindowSeconds;
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
            kInternalStorageUserProjectionRateLimitWindowSeconds) {
            window.started_at = now;
            window.requests = 1;
            return true;
        }

        if (window.requests >=
            kInternalStorageUserProjectionRateLimitRequests) {
            if (retry_after_seconds) {
                *retry_after_seconds =
                    std::max(
                        1L,
                        kInternalStorageUserProjectionRateLimitWindowSeconds -
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
                kInternalStorageUserProjectionRateLimitWindowSeconds) {
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

class DefaultStorageUserProjectionAuditRateLimiter {
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
                : remote_address;

        const auto now =
            Clock::now();

        std::lock_guard<std::mutex> lock(
            mutex_
        );

        prune_expired(now);

        auto found =
            windows_.find(peer);

        if (found == windows_.end()) {
            if (windows_.size() >=
                kInternalStorageUserProjectionAuditRateLimitTrackedPeers) {
                if (retry_after_seconds) {
                    *retry_after_seconds =
                        kInternalStorageUserProjectionAuditRateLimitWindowSeconds;
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
            kInternalStorageUserProjectionAuditRateLimitWindowSeconds) {
            window.started_at = now;
            window.requests = 1;
            return true;
        }

        if (window.requests >=
            kInternalStorageUserProjectionAuditRateLimitRequests) {
            if (retry_after_seconds) {
                *retry_after_seconds =
                    std::max(
                        1L,
                        kInternalStorageUserProjectionAuditRateLimitWindowSeconds -
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
                kInternalStorageUserProjectionAuditRateLimitWindowSeconds) {
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

bool projection_context_ok(
    const InternalStorageUserProjectionRoutesContext& context
) noexcept {
    return
        static_cast<bool>(
            context.verify_authorization
        ) &&
        context.projection_store &&
        valid_cluster_node_id(
            context.local_node_id) &&
        static_cast<bool>(
            context.consume_rate_limit_slot
        );
}

bool audit_context_ok(
    const InternalStorageUserProjectionRoutesContext& context
) noexcept {
    return
        static_cast<bool>(
            context.verify_authorization
        ) &&
        context.projection_store &&
        valid_cluster_node_id(
            context.local_node_id) &&
        static_cast<bool>(
            context.consume_audit_rate_limit_slot
        );
}

void reply_json_local(
    const InternalStorageUserProjectionRoutesContext& context,
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
    const InternalStorageUserProjectionRoutesContext& context,
    httplib::Response& response
) {
    response.set_header(
        "WWW-Authenticate",
        "Bearer"
    );

    // Security: do not disclose whether service authentication failed because
    // the header was missing, duplicated, malformed, or contained a bad token.
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

    auto iterator =
        range.first;

    if (iterator == range.second) {
        return nullptr;
    }

    const std::string* value =
        &iterator->second;

    ++iterator;

    // Security: reject duplicate Authorization fields so an intermediary and
    // this process cannot authenticate different bearer credentials.
    if (iterator != range.second ||
        value->empty() ||
        value->size() >
            kMaximumAuthorizationHeaderBytes) {
        return nullptr;
    }

    // Avoid another plaintext credential copy.
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

    auto iterator =
        range.first;

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

bool has_only_keys(
    const json& object,
    std::initializer_list<const char*> allowed
) {
    if (!object.is_object()) {
        return false;
    }

    for (auto iterator = object.begin();
         iterator != object.end();
         ++iterator) {
        bool found = false;

        for (const char* key : allowed) {
            if (iterator.key() == key) {
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }

    return true;
}

bool json_string(
    const json& object,
    const char* key,
    std::string* output
) {
    if (!output) {
        return false;
    }

    const auto iterator =
        object.find(key);

    if (iterator == object.end() ||
        !iterator->is_string()) {
        return false;
    }

    *output =
        iterator->get<std::string>();

    return true;
}

bool json_bool(
    const json& object,
    const char* key,
    bool* output
) {
    if (!output) {
        return false;
    }

    const auto iterator =
        object.find(key);

    if (iterator == object.end() ||
        !iterator->is_boolean()) {
        return false;
    }

    *output =
        iterator->get<bool>();

    return true;
}

bool json_u64(
    const json& object,
    const char* key,
    std::uint64_t* output
) {
    if (!output) {
        return false;
    }

    const auto iterator =
        object.find(key);

    if (iterator == object.end()) {
        return false;
    }

    try {
        if (iterator->is_number_unsigned()) {
            *output =
                iterator->get<std::uint64_t>();
            return true;
        }

        if (iterator->is_number_integer()) {
            const std::int64_t value =
                iterator->get<std::int64_t>();

            if (value < 0) {
                return false;
            }

            *output =
                static_cast<std::uint64_t>(
                    value
                );

            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool parse_projection_body(
    const json& body,
    StorageUserProjection* projection
) {
    if (!projection ||
        !has_only_keys(
            body,
            {
                "fingerprint",
                "enabled",
                "storage_allocated",
                "quota_bytes",
                "root_rel",
                "storage_pool_id",
                "home_node",
                "revision"
            })) {
        return false;
    }

    StorageUserProjection parsed;

    if (!json_string(
            body,
            "fingerprint",
            &parsed.fingerprint) ||
        !json_bool(
            body,
            "enabled",
            &parsed.enabled) ||
        !json_bool(
            body,
            "storage_allocated",
            &parsed.storage_allocated) ||
        !json_u64(
            body,
            "quota_bytes",
            &parsed.quota_bytes) ||
        !json_string(
            body,
            "root_rel",
            &parsed.root_rel) ||
        !json_string(
            body,
            "storage_pool_id",
            &parsed.storage_pool_id) ||
        !json_string(
            body,
            "home_node",
            &parsed.home_node) ||
        !json_u64(
            body,
            "revision",
            &parsed.revision)) {
        return false;
    }

    *projection =
        std::move(parsed);

    return true;
}

bool valid_audit_digest_hex(
    const std::string& value
) {
    if (value.size() !=
        kStorageUserProjectionDigestHexChars) {
        return false;
    }

    for (unsigned char character : value) {
        const bool valid =
            (character >= '0' && character <= '9') ||
            (character >= 'a' && character <= 'f');

        if (!valid) {
            return false;
        }
    }

    return true;
}

bool fingerprint_in_audit_range(
    const std::string& fingerprint,
    const std::string& range_start,
    const std::string& range_end
) {
    return
        (range_start.empty() ||
         fingerprint >= range_start) &&
        (range_end.empty() ||
         fingerprint < range_end);
}

bool parse_projection_audit_body(
    const json& body,
    std::string* node_id,
    std::string* range_start,
    std::string* range_end,
    std::vector<
        StorageUserProjectionAuditExpected
    >* expected
) {
    if (!node_id ||
        !range_start ||
        !range_end ||
        !expected ||
        !has_only_keys(
            body,
            {
                "version",
                "node_id",
                "range_start",
                "range_end",
                "expected"
            })) {
        return false;
    }

    std::uint64_t version = 0;

    if (!json_u64(
            body,
            "version",
            &version) ||
        version != 1 ||
        !json_string(
            body,
            "node_id",
            node_id) ||
        !json_string(
            body,
            "range_start",
            range_start) ||
        !json_string(
            body,
            "range_end",
            range_end)) {
        return false;
    }

    if (!valid_cluster_node_id(
            *node_id) ||
        (!range_start->empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             *range_start)) ||
        (!range_end->empty() &&
         !pqnas::valid_storage_user_fingerprint_hex(
             *range_end)) ||
        (!range_start->empty() &&
         !range_end->empty() &&
         *range_start >= *range_end)) {
        return false;
    }

    const auto expected_iterator =
        body.find("expected");

    if (expected_iterator == body.end() ||
        !expected_iterator->is_array() ||
        expected_iterator->size() >
            kStorageUserProjectionAuditMaxExpected) {
        return false;
    }

    std::vector<
        StorageUserProjectionAuditExpected
    > parsed;

    parsed.reserve(
        expected_iterator->size()
    );

    std::string previous_fingerprint;
    bool have_previous_fingerprint = false;

    for (const auto& item :
         *expected_iterator) {
        if (!has_only_keys(
                item,
                {
                    "fingerprint",
                    "revision",
                    "digest"
                })) {
            return false;
        }

        StorageUserProjectionAuditExpected
            entry;

        if (!json_string(
                item,
                "fingerprint",
                &entry.fingerprint) ||
            !json_u64(
                item,
                "revision",
                &entry.revision) ||
            !json_string(
                item,
                "digest",
                &entry.digest_hex) ||
            !pqnas::valid_storage_user_fingerprint_hex(
                entry.fingerprint) ||
            entry.revision == 0 ||
            !valid_audit_digest_hex(
                entry.digest_hex) ||
            !fingerprint_in_audit_range(
                entry.fingerprint,
                *range_start,
                *range_end) ||
            (have_previous_fingerprint &&
             entry.fingerprint <=
                 previous_fingerprint)) {
            return false;
        }

        previous_fingerprint =
            entry.fingerprint;
        have_previous_fingerprint = true;

        parsed.push_back(
            std::move(entry)
        );
    }

    *expected =
        std::move(parsed);

    return true;
}

} // namespace

void handle_internal_storage_user_projection_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalStorageUserProjectionRoutesContext& context
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    response.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );

    if (!projection_context_ok(context)) {
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

    bool authorized = false;

    if (authorization) {
        try {
            authorized =
                context.verify_authorization(
                    *authorization
                );
        } catch (...) {
            // Security: authentication dependency failures fail closed.
            authorized = false;
        }
    }

    if (!authorized) {
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
        // Security: limiter failure must fail closed.
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {"error", "projection_unavailable"}
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

    if (!exact_json_content_type(
            request)) {
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
            kInternalStorageUserProjectionMaxRequestBytes) {
        reply_json_local(
            context,
            response,
            request.body.empty()
                ? 400
                : 413,
            json{
                {
                    "ok",
                    false
                },
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

    json body =
        json::parse(
            request.body,
            nullptr,
            false
        );

    if (body.is_discarded() ||
        !body.is_object()) {
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

    StorageUserProjection projection;

    if (!parse_projection_body(
            body,
            &projection)) {
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

    if (!valid_cluster_node_id(
            projection.home_node)) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "invalid_projection"}
            }
        );

        return;
    }

    // Security: a valid projection for another Node must never be installed
    // locally, even when the caller possesses the cluster bearer credential.
    if (projection.home_node !=
        context.local_node_id) {
        reply_json_local(
            context,
            response,
            409,
            json{
                {"ok", false},
                {"error", "wrong_node"}
            }
        );

        return;
    }

    StorageUserProjectionStoreResult result;

    try {
        result =
            context.projection_store->apply(
                projection,
                nullptr
            );
    } catch (...) {
        // Security: do not expose filesystem, parser, allocation or persistence
        // details across the internal control-plane boundary.
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {"error", "projection_unavailable"}
            }
        );

        return;
    }

    switch (result) {
        case StorageUserProjectionStoreResult::Applied:
            reply_json_local(
                context,
                response,
                200,
                json{
                    {"ok", true},
                    {"result", "applied"},
                    {"revision", projection.revision}
                }
            );
            return;

        case StorageUserProjectionStoreResult::Unchanged:
            reply_json_local(
                context,
                response,
                200,
                json{
                    {"ok", true},
                    {"result", "unchanged"},
                    {"revision", projection.revision}
                }
            );
            return;

        case StorageUserProjectionStoreResult::Stale:
            reply_json_local(
                context,
                response,
                409,
                json{
                    {"ok", false},
                    {"error", "stale_projection"}
                }
            );
            return;

        case StorageUserProjectionStoreResult::Conflict:
            reply_json_local(
                context,
                response,
                409,
                json{
                    {"ok", false},
                    {"error", "projection_conflict"}
                }
            );
            return;

        case StorageUserProjectionStoreResult::InvalidInput:
            reply_json_local(
                context,
                response,
                400,
                json{
                    {"ok", false},
                    {"error", "invalid_projection"}
                }
            );
            return;

        case StorageUserProjectionStoreResult::PersistFailed:
            reply_json_local(
                context,
                response,
                503,
                json{
                    {"ok", false},
                    {"error", "projection_unavailable"}
                }
            );
            return;
    }

    reply_json_local(
        context,
        response,
        503,
        json{
            {"ok", false},
            {"error", "projection_unavailable"}
        }
    );
}

void handle_internal_storage_user_projection_audit_request(
    const httplib::Request& request,
    httplib::Response& response,
    const InternalStorageUserProjectionRoutesContext& context
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    response.set_header(
        "X-Content-Type-Options",
        "nosniff"
    );

    if (!audit_context_ok(context)) {
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

    bool authorized = false;

    if (authorization) {
        try {
            authorized =
                context.verify_authorization(
                    *authorization
                );
        } catch (...) {
            // Security: verifier dependency failures fail closed before any
            // Storage projection snapshot is materialized.
            authorized = false;
        }
    }

    if (!authorized) {
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
            context.consume_audit_rate_limit_slot(
                request.remote_addr,
                &retry_after_seconds
            );
    } catch (...) {
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {"error", "audit_unavailable"}
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

    if (!exact_json_content_type(
            request)) {
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
            kInternalStorageUserProjectionAuditMaxRequestBytes) {
        reply_json_local(
            context,
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

    json body =
        json::parse(
            request.body,
            nullptr,
            false
        );

    if (body.is_discarded() ||
        !body.is_object()) {
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

    std::string requested_node_id;
    std::string range_start;
    std::string range_end;

    std::vector<
        StorageUserProjectionAuditExpected
    > expected;

    if (!parse_projection_audit_body(
            body,
            &requested_node_id,
            &range_start,
            &range_end,
            &expected)) {
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

    // Security: even an authenticated Director must explicitly bind an audit
    // manifest to the exact Storage Node that is answering it.
    if (requested_node_id !=
        context.local_node_id) {
        reply_json_local(
            context,
            response,
            409,
            json{
                {"ok", false},
                {"error", "wrong_node"}
            }
        );

        return;
    }

    StorageUserProjectionAuditResult
        audit_result;

    bool audit_ok = false;

    try {
        const auto snapshot =
            context.projection_store->snapshot();

        audit_ok =
            audit_storage_user_projection_range(
                context.local_node_id,
                range_start,
                range_end,
                expected,
                snapshot,
                &audit_result,
                nullptr
            );
    } catch (...) {
        audit_ok = false;
    }

    if (!audit_ok) {
        // Request syntax has already been fully validated. Failure here means
        // Storage state/digest verification itself could not be trusted.
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {"error", "audit_unavailable"}
            }
        );

        return;
    }

    json mismatches =
        json::array();

    for (const auto& mismatch :
         audit_result.mismatches) {
        mismatches.push_back(
            json{
                {
                    "fingerprint",
                    mismatch.fingerprint
                },
                {
                    "kind",
                    storage_user_projection_audit_mismatch_code(
                        mismatch.code
                    )
                },
                {
                    "expected_revision",
                    mismatch.expected_revision
                },
                {
                    "actual_revision",
                    mismatch.actual_revision
                }
            }
        );
    }

    reply_json_local(
        context,
        response,
        200,
        json{
            {"version", 1},
            {"ok", true},
            {
                "node_id",
                context.local_node_id
            },
            {
                "complete",
                audit_result.complete
            },
            {
                "matched",
                audit_result.matched
            },
            {
                "safe_tombstones_ignored",
                audit_result.safe_tombstones_ignored
            },
            {
                "mismatches",
                std::move(mismatches)
            }
        }
    );
}

void register_internal_storage_user_projection_routes(
    httplib::Server& server,
    const InternalStorageUserProjectionRoutesContext& context
) {
    InternalStorageUserProjectionRoutesContext
        captured = context;

    if (!captured.consume_rate_limit_slot) {
        auto limiter =
            std::make_shared<
                DefaultStorageUserProjectionRateLimiter
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

    if (!captured.consume_audit_rate_limit_slot) {
        auto limiter =
            std::make_shared<
                DefaultStorageUserProjectionAuditRateLimiter
            >();

        captured.consume_audit_rate_limit_slot =
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
        "/internal/storage/user-projection",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_internal_storage_user_projection_request(
                request,
                response,
                captured
            );
        }
    );

    server.Post(
        "/internal/storage/user-projection/audit",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_internal_storage_user_projection_audit_request(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
