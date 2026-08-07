#include "routes/routes_node_enrollment_bootstrap.h"

#include "cluster/internal_auth.h"
#include "cluster/node_enrollment_consumption_service.h"
#include "cluster/node_enrollment_token.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <deque>
#include <initializer_list>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t kRateBucketCap = 4096;
constexpr std::size_t kMaximumApiBaseUrlBytes = 2048;

std::mutex g_rate_mutex;

std::unordered_map<
    std::string,
    std::deque<std::int64_t>
> g_rate_buckets;

class EnrollmentTokenWiper {
public:
    explicit EnrollmentTokenWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~EnrollmentTokenWiper() {
        clear_node_enrollment_token_text(
            &value_
        );
    }

    EnrollmentTokenWiper(
        const EnrollmentTokenWiper&
    ) = delete;

    EnrollmentTokenWiper& operator=(
        const EnrollmentTokenWiper&
    ) = delete;

private:
    std::string& value_;
};

class CredentialWiper {
public:
    explicit CredentialWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~CredentialWiper() {
        clear_internal_auth_token_text(
            &value_
        );
    }

    CredentialWiper(
        const CredentialWiper&
    ) = delete;

    CredentialWiper& operator=(
        const CredentialWiper&
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
            object->find("node_credential");

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
        // Best-effort wipe only. Request processing has already completed.
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

bool context_ok(
    const NodeEnrollmentBootstrapRoutesContext&
        context
) noexcept {
    return
        context.service &&
        static_cast<bool>(
            context.now_iso_utc
        );
}

void reply_json_local(
    const NodeEnrollmentBootstrapRoutesContext&
        context,
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

std::string trim_ascii(
    const std::string& input
) {
    std::size_t begin = 0;

    while (begin < input.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   input[begin]))) {
        ++begin;
    }

    std::size_t end = input.size();

    while (end > begin &&
           std::isspace(
               static_cast<unsigned char>(
                   input[end - 1]))) {
        --end;
    }

    return input.substr(
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
        [](unsigned char ch) {
            return static_cast<char>(
                std::tolower(ch)
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

    // Security: duplicated Content-Type headers can be interpreted
    // differently by proxies and the application.
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

bool exact_bearer_token(
    const httplib::Request& request,
    std::string* token
) {
    if (!token) {
        return false;
    }

    clear_node_enrollment_token_text(
        token
    );

    const auto range =
        request.headers.equal_range(
            "Authorization"
        );

    auto iterator = range.first;

    if (iterator == range.second) {
        return false;
    }

    const std::string value =
        iterator->second;

    ++iterator;

    // Security: reject duplicated Authorization headers so no intermediary
    // can authenticate a different bootstrap token from the application.
    if (iterator != range.second ||
        value.size() >
            kNodeEnrollmentBootstrapAuthorizationMaxBytes) {
        return false;
    }

    static const std::string prefix =
        "Bearer ";

    if (value.size() !=
            prefix.size() +
                kNodeEnrollmentTokenChars ||
        value.compare(
            0,
            prefix.size(),
            prefix) != 0) {
        return false;
    }

    try {
        token->assign(
            value,
            prefix.size(),
            kNodeEnrollmentTokenChars
        );
    } catch (...) {
        clear_node_enrollment_token_text(
            token
        );

        return false;
    }

    std::string ignored_hash;

    const NodeEnrollmentTokenResult result =
        hash_node_enrollment_token(
            *token,
            &ignored_hash
        );

    clear_node_enrollment_token_text(
        &ignored_hash
    );

    if (result !=
        NodeEnrollmentTokenResult::Ok) {
        clear_node_enrollment_token_text(
            token
        );

        return false;
    }

    return true;
}

std::int64_t now_epoch_fallback() {
    return static_cast<std::int64_t>(
        std::time(nullptr)
    );
}

std::int64_t now_epoch(
    const NodeEnrollmentBootstrapRoutesContext&
        context
) {
    if (context.now_epoch_sec) {
        return context.now_epoch_sec();
    }

    return now_epoch_fallback();
}

std::string rate_key(
    const httplib::Request& request
) {
    if (request.remote_addr.empty()) {
        return "unknown";
    }

    // Security: remote_addr is only an opaque, bounded in-memory key.
    if (request.remote_addr.size() > 128) {
        return "oversized";
    }

    return request.remote_addr;
}

void compact_rate_buckets_locked(
    std::int64_t now
) {
    const std::int64_t minimum =
        now -
        kNodeEnrollmentBootstrapRateWindowSeconds;

    for (auto iterator =
             g_rate_buckets.begin();
         iterator !=
             g_rate_buckets.end();) {
        auto& hits =
            iterator->second;

        while (!hits.empty() &&
               hits.front() < minimum) {
            hits.pop_front();
        }

        if (hits.empty()) {
            iterator =
                g_rate_buckets.erase(
                    iterator
                );
        } else {
            ++iterator;
        }
    }
}

bool rate_limited(
    const httplib::Request& request,
    std::int64_t now
) {
    const std::string key =
        rate_key(request);

    const std::int64_t minimum =
        now -
        kNodeEnrollmentBootstrapRateWindowSeconds;

    std::lock_guard<std::mutex> lock(
        g_rate_mutex
    );

    if (g_rate_buckets.size() >=
        kRateBucketCap) {
        compact_rate_buckets_locked(
            now
        );
    }

    auto found =
        g_rate_buckets.find(key);

    // Security: keep the attacker-controlled key map bounded.
    if (g_rate_buckets.size() >=
            kRateBucketCap &&
        found == g_rate_buckets.end()) {
        return true;
    }

    auto& hits =
        g_rate_buckets[key];

    while (!hits.empty() &&
           hits.front() < minimum) {
        hits.pop_front();
    }

    if (static_cast<int>(
            hits.size()) >=
        kNodeEnrollmentBootstrapRateLimit) {
        return true;
    }

    hits.push_back(now);
    return false;
}

bool has_only_keys(
    const json& object,
    std::initializer_list<const char*>
        allowed
) {
    if (!object.is_object()) {
        return false;
    }

    for (auto iterator =
             object.begin();
         iterator != object.end();
         ++iterator) {
        bool found = false;

        for (const char* allowed_key :
             allowed) {
            if (iterator.key() ==
                allowed_key) {
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
    std::size_t maximum_bytes,
    std::string* output
) {
    if (!output) {
        return false;
    }

    output->clear();

    const auto iterator =
        object.find(key);

    if (iterator == object.end() ||
        !iterator->is_string()) {
        return false;
    }

    try {
        *output =
            iterator->get<std::string>();
    } catch (...) {
        output->clear();
        return false;
    }

    if (output->empty() ||
        output->size() > maximum_bytes) {
        output->clear();
        return false;
    }

    return true;
}

bool json_int(
    const json& object,
    const char* key,
    int* output
) {
    if (!output) {
        return false;
    }

    const auto iterator =
        object.find(key);

    if (iterator == object.end() ||
        !iterator->is_number_integer()) {
        return false;
    }

    try {
        const std::int64_t value =
            iterator->get<std::int64_t>();

        if (value <
                std::numeric_limits<int>::
                    min() ||
            value >
                std::numeric_limits<int>::
                    max()) {
            return false;
        }

        *output =
            static_cast<int>(value);
    } catch (...) {
        return false;
    }

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

    if (iterator == object.end() ||
        !iterator->is_number_integer()) {
        return false;
    }

    try {
        if (iterator->is_number_unsigned()) {
            *output =
                iterator->get<std::uint64_t>();

            return *output != 0;
        }

        const std::int64_t value =
            iterator->get<std::int64_t>();

        if (value <= 0) {
            return false;
        }

        *output =
            static_cast<std::uint64_t>(
                value
            );
    } catch (...) {
        return false;
    }

    return true;
}

int service_http_status(
    NodeEnrollmentConsumptionServiceResult result
) noexcept {
    switch (result) {
        case NodeEnrollmentConsumptionServiceResult::Ok:
            return 200;

        case NodeEnrollmentConsumptionServiceResult::
                InvalidInput:
            return 400;

        case NodeEnrollmentConsumptionServiceResult::
                NotFound:
        case NodeEnrollmentConsumptionServiceResult::
                GenerationMismatch:
        case NodeEnrollmentConsumptionServiceResult::
                Expired:
        case NodeEnrollmentConsumptionServiceResult::
                Revoked:
        case NodeEnrollmentConsumptionServiceResult::
                Consumed:
        case NodeEnrollmentConsumptionServiceResult::
                NodeMismatch:
        case NodeEnrollmentConsumptionServiceResult::
                UrlMismatch:
        case NodeEnrollmentConsumptionServiceResult::
                ProtocolIncompatible:
        case NodeEnrollmentConsumptionServiceResult::
                BootstrapTokenInvalid:
            return 401;

        case NodeEnrollmentConsumptionServiceResult::
                ProvisioningStateConflict:
        case NodeEnrollmentConsumptionServiceResult::
                CredentialConflict:
        case NodeEnrollmentConsumptionServiceResult::
                NodeConflict:
            return 409;

        case NodeEnrollmentConsumptionServiceResult::
                ServiceUnavailable:
        case NodeEnrollmentConsumptionServiceResult::
                CredentialUnavailable:
        case NodeEnrollmentConsumptionServiceResult::
                PersistFailed:
        case NodeEnrollmentConsumptionServiceResult::
                RegistryFailed:
            return 503;
    }

    return 503;
}

bool authentication_failure(
    NodeEnrollmentConsumptionServiceResult result
) noexcept {
    return
        service_http_status(result) ==
        401;
}

void reply_authentication_failed(
    const NodeEnrollmentBootstrapRoutesContext&
        context,
    httplib::Response& response
) {
    response.set_header(
        "WWW-Authenticate",
        "Bearer"
    );

    // Security: use one response for all missing, stale, terminal and
    // mismatching enrollment identities to avoid a bootstrap-state oracle.
    reply_json_local(
        context,
        response,
        401,
        json{
            {"ok", false},
            {
                "error",
                "enrollment_auth_failed"
            }
        }
    );
}

bool current_timestamp(
    const NodeEnrollmentBootstrapRoutesContext&
        context,
    std::string* output
) {
    if (!output) {
        return false;
    }

    output->clear();

    try {
        *output =
            context.now_iso_utc();
    } catch (...) {
        output->clear();
    }

    // Security/reliability: the shared runtime clock may emit millisecond
    // precision, while enrollment persistence accepts canonical UTC seconds.
    // Normalize only the exact ".dddZ" suffix and leave all other input for
    // the strict service-layer timestamp validator.
    if (output->size() == 24 &&
        (*output)[19] == '.' &&
        (*output)[20] >= '0' &&
        (*output)[20] <= '9' &&
        (*output)[21] >= '0' &&
        (*output)[21] <= '9' &&
        (*output)[22] >= '0' &&
        (*output)[22] <= '9' &&
        (*output)[23] == 'Z') {
        output->erase(19, 4);
    }

    return !output->empty();
}

} // namespace

void handle_node_enrollment_bootstrap_consume(
    const httplib::Request& request,
    httplib::Response& response,
    const NodeEnrollmentBootstrapRoutesContext&
        context
) {
    response.set_header(
        "Cache-Control",
        "no-store"
    );

    if (!context_ok(context)) {
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {
                    "error",
                    "enrollment_consumption_service_unavailable"
                }
            }
        );

        return;
    }

    if (rate_limited(
            request,
            now_epoch(context))) {
        response.set_header(
            "Retry-After",
            "60"
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

    std::string bootstrap_token;
    EnrollmentTokenWiper bootstrap_wiper(
        bootstrap_token
    );

    if (!exact_bearer_token(
            request,
            &bootstrap_token)) {
        reply_authentication_failed(
            context,
            response
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
                {
                    "error",
                    "unsupported_media_type"
                }
            }
        );

        return;
    }

    if (request.body.size() >
        kNodeEnrollmentBootstrapBodyMaxBytes) {
        reply_json_local(
            context,
            response,
            413,
            json{
                {"ok", false},
                {"error", "payload_too_large"},
                {
                    "max_bytes",
                    kNodeEnrollmentBootstrapBodyMaxBytes
                }
            }
        );

        return;
    }

    json body;

    try {
        body =
            json::parse(request.body);
    } catch (...) {
        reply_json_local(
            context,
            response,
            400,
            json{
                {"ok", false},
                {"error", "bad_json"}
            }
        );

        return;
    }

    JsonCredentialWiper body_wiper(body);

    if (!body.is_object() ||
        !has_only_keys(
            body,
            {
                "enrollment_id",
                "expected_generation",
                "node_id",
                "api_base_url",
                "protocol_version",
                "node_credential"
            })) {
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

    NodeEnrollmentConsumptionRequest
        service_request;

    if (!json_string(
            body,
            "enrollment_id",
            128,
            &service_request.enrollment_id) ||
        !json_u64(
            body,
            "expected_generation",
            &service_request
                 .expected_generation) ||
        !json_string(
            body,
            "node_id",
            128,
            &service_request.node_id) ||
        !json_string(
            body,
            "api_base_url",
            kMaximumApiBaseUrlBytes,
            &service_request.api_base_url) ||
        !json_int(
            body,
            "protocol_version",
            &service_request
                 .protocol_version) ||
        !json_string(
            body,
            "node_credential",
            kInternalAuthTokenChars,
            &service_request.node_credential) ||
        service_request.node_credential.size() !=
            kInternalAuthTokenChars) {
        clear_internal_auth_token_text(
            &service_request.node_credential
        );

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

    CredentialWiper credential_wiper(
        service_request.node_credential
    );

    service_request.bootstrap_token =
        bootstrap_token;

    EnrollmentTokenWiper request_token_wiper(
        service_request.bootstrap_token
    );

    if (!current_timestamp(
            context,
            &service_request.now_iso)) {
        reply_json_local(
            context,
            response,
            503,
            json{
                {"ok", false},
                {
                    "error",
                    "enrollment_consumption_service_unavailable"
                }
            }
        );

        return;
    }

    const NodeEnrollmentConsumptionResult result =
        context.service->consume(
            service_request
        );

    if (result.ok) {
        reply_json_local(
            context,
            response,
            200,
            json{
                {"ok", true},
                {
                    "recovered",
                    result.recovered
                }
            }
        );

        return;
    }

    if (authentication_failure(
            result.result)) {
        reply_authentication_failed(
            context,
            response
        );

        return;
    }

    reply_json_local(
        context,
        response,
        service_http_status(
            result.result
        ),
        json{
            {"ok", false},
            {
                "error",
                result.error_code()
            }
        }
    );
}

void register_node_enrollment_bootstrap_routes(
    httplib::Server& server,
    const NodeEnrollmentBootstrapRoutesContext&
        context
) {
    const NodeEnrollmentBootstrapRoutesContext
        captured = context;

    server.Post(
        "/internal/node/enrollments/consume",
        [captured](
            const httplib::Request& request,
            httplib::Response& response
        ) {
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
