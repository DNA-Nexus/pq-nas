#include "routes/routes_admin_node_enrollments.h"

#include "cluster/node_enrollment_service.h"
#include "cluster/node_enrollment_token.h"
#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_node_enrollment_token_text(
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

bool context_ok(
    const AdminNodeEnrollmentRoutesContext&
        context
) {
    return
        context.service &&
        static_cast<bool>(
            context.require_admin_auth
        ) &&
        static_cast<bool>(
            context.require_same_origin
        ) &&
        static_cast<bool>(
            context.reply_json
        ) &&
        static_cast<bool>(
            context.now_iso_utc
        ) &&
        static_cast<bool>(
            context.add_seconds_iso_utc
        );
}

void reply_json_text_local(
    const AdminNodeEnrollmentRoutesContext&
        context,
    httplib::Response& res,
    int status,
    const std::string& body
) {
    res.set_header(
        "Cache-Control",
        "no-store"
    );

    if (context.reply_json) {
        context.reply_json(
            res,
            status,
            body
        );

        return;
    }

    res.status = status;

    res.set_content(
        body,
        "application/json; charset=utf-8"
    );
}

void reply_json_local(
    const AdminNodeEnrollmentRoutesContext&
        context,
    httplib::Response& res,
    int status,
    const json& body
) {
    reply_json_text_local(
        context,
        res,
        status,
        body.dump()
    );
}

bool has_only_keys(
    const json& object,
    std::initializer_list<const char*>
        allowed
) {
    if (!object.is_object()) {
        return false;
    }

    for (auto iterator = object.begin();
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

    try {
        *output =
            iterator->get<std::string>();
    } catch (...) {
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

            return true;
        }

        const std::int64_t signed_value =
            iterator->get<std::int64_t>();

        if (signed_value < 0) {
            return false;
        }

        *output =
            static_cast<std::uint64_t>(
                signed_value
            );
    } catch (...) {
        return false;
    }

    return true;
}

bool parse_json_object(
    const AdminNodeEnrollmentRoutesContext&
        context,
    const httplib::Request& req,
    httplib::Response& res,
    json* output
) {
    if (!output) {
        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "request parser unavailable"
                }
            }
        );

        return false;
    }

    if (req.body.size() >
        kAdminNodeEnrollmentRequestBodyMaxBytes) {
        reply_json_local(
            context,
            res,
            413,
            json{
                {"ok", false},
                {"error", "payload_too_large"},
                {
                    "message",
                    "request body is too large"
                },
                {
                    "max_bytes",
                    kAdminNodeEnrollmentRequestBodyMaxBytes
                }
            }
        );

        return false;
    }

    try {
        *output =
            json::parse(req.body);
    } catch (...) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_json"},
                {
                    "message",
                    "invalid JSON body"
                }
            }
        );

        return false;
    }

    if (!output->is_object()) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "JSON body must be an object"
                }
            }
        );

        return false;
    }

    return true;
}

bool authenticate_admin(
    const AdminNodeEnrollmentRoutesContext&
        context,
    const httplib::Request& req,
    httplib::Response& res,
    std::string* actor_fingerprint
) {
    res.set_header(
        "Cache-Control",
        "no-store"
    );

    if (!context_ok(context)) {
        reply_json_local(
            context,
            res,
            503,
            json{
                {"ok", false},
                {"error", "service_unavailable"},
                {
                    "message",
                    "node enrollment service unavailable"
                }
            }
        );

        return false;
    }

    if (!actor_fingerprint) {
        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "authentication output unavailable"
                }
            }
        );

        return false;
    }

    actor_fingerprint->clear();

    if (!context.require_admin_auth(
            req,
            res,
            actor_fingerprint)) {
        return false;
    }

    if (actor_fingerprint->empty()) {
        reply_json_local(
            context,
            res,
            403,
            json{
                {"ok", false},
                {"error", "forbidden"},
                {
                    "message",
                    "invalid authenticated fingerprint"
                }
            }
        );

        return false;
    }

    return true;
}

bool authorize_mutation(
    const AdminNodeEnrollmentRoutesContext&
        context,
    const httplib::Request& req,
    httplib::Response& res,
    std::string* actor_fingerprint
) {
    if (!authenticate_admin(
            context,
            req,
            res,
            actor_fingerprint)) {
        return false;
    }

    if (!context.require_same_origin(
            req,
            res)) {
        return false;
    }

    return true;
}

bool current_timestamp(
    const AdminNodeEnrollmentRoutesContext&
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

    return !output->empty();
}

bool create_time_window(
    const AdminNodeEnrollmentRoutesContext&
        context,
    std::uint64_t ttl_seconds,
    std::string* created_at,
    std::string* expires_at
) {
    if (!created_at || !expires_at) {
        return false;
    }

    created_at->clear();
    expires_at->clear();

    if (!current_timestamp(
            context,
            created_at)) {
        return false;
    }

    try {
        if (!context.add_seconds_iso_utc(
                *created_at,
                ttl_seconds,
                expires_at)) {
            expires_at->clear();
            return false;
        }
    } catch (...) {
        expires_at->clear();
        return false;
    }

    return
        !expires_at->empty() &&
        *created_at < *expires_at;
}

bool valid_bootstrap_token_text(
    const std::string& token
) {
    if (token.size() !=
        kNodeEnrollmentTokenChars) {
        return false;
    }

    for (unsigned char ch : token) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_') {
            return false;
        }
    }

    return true;
}

json enrollment_to_json(
    const NodeEnrollmentView& enrollment
) {
    // This is the redacted service DTO. It deliberately has no verifier.
    return json{
        {
            "enrollment_id",
            enrollment.enrollment_id
        },
        {"node_id", enrollment.node_id},
        {
            "api_base_url",
            enrollment.api_base_url
        },
        {
            "required_protocol_version",
            enrollment.required_protocol_version
        },
        {
            "created_at",
            enrollment.created_at
        },
        {
            "expires_at",
            enrollment.expires_at
        },
        {"state", enrollment.state},
        {
            "revoked_at",
            enrollment.revoked_at
        },
        {
            "consumed_at",
            enrollment.consumed_at
        },
        {
            "generation",
            enrollment.generation
        }
    };
}

int service_result_http_status(
    NodeEnrollmentServiceResult result
) {
    switch (result) {
        case NodeEnrollmentServiceResult::Ok:
            return 200;

        case NodeEnrollmentServiceResult::
                InvalidInput:
            return 400;

        case NodeEnrollmentServiceResult::
                NotFound:
            return 404;

        case NodeEnrollmentServiceResult::
                EnrollmentIdConflict:
        case NodeEnrollmentServiceResult::
                UrlConflict:
        case NodeEnrollmentServiceResult::
                GenerationMismatch:
        case NodeEnrollmentServiceResult::
                Revoked:
        case NodeEnrollmentServiceResult::
                Consumed:
            return 409;

        case NodeEnrollmentServiceResult::
                ServiceUnavailable:
            return 503;

        case NodeEnrollmentServiceResult::
                IdGenerationFailed:
        case NodeEnrollmentServiceResult::
                TokenGenerationFailed:
        case NodeEnrollmentServiceResult::
                PersistFailed:
        case NodeEnrollmentServiceResult::
                RegistryFailed:
            return 500;
    }

    return 500;
}

void reply_service_failure(
    const AdminNodeEnrollmentRoutesContext&
        context,
    httplib::Response& res,
    NodeEnrollmentServiceResult result
) {
    reply_json_local(
        context,
        res,
        service_result_http_status(
            result
        ),
        json{
            {"ok", false},
            {
                "error",
                node_enrollment_service_result_code(
                    result
                )
            },
            {
                "message",
                "node enrollment operation failed"
            }
        }
    );
}

} // namespace

void handle_admin_node_enrollments_list(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminNodeEnrollmentRoutesContext&
        context
) {
    std::string actor_fingerprint;

    if (!authenticate_admin(
            context,
            req,
            res,
            &actor_fingerprint)) {
        return;
    }

    std::vector<NodeEnrollmentView>
        enrollments =
            context.service->
                redacted_snapshot();

    std::sort(
        enrollments.begin(),
        enrollments.end(),
        [](
            const NodeEnrollmentView& left,
            const NodeEnrollmentView& right
        ) {
            if (left.created_at !=
                right.created_at) {
                return left.created_at >
                    right.created_at;
            }

            return left.enrollment_id <
                right.enrollment_id;
        }
    );

    json items = json::array();

    for (const NodeEnrollmentView& enrollment :
         enrollments) {
        items.push_back(
            enrollment_to_json(enrollment)
        );
    }

    reply_json_local(
        context,
        res,
        200,
        json{
            {"ok", true},
            {
                "count",
                enrollments.size()
            },
            {
                "enrollments",
                std::move(items)
            }
        }
    );
}

void handle_admin_node_enrollments_create(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminNodeEnrollmentRoutesContext&
        context
) {
    std::string actor_fingerprint;

    if (!authorize_mutation(
            context,
            req,
            res,
            &actor_fingerprint)) {
        return;
    }

    json body;

    if (!parse_json_object(
            context,
            req,
            res,
            &body)) {
        return;
    }

    if (!has_only_keys(
            body,
            {
                "node_id",
                "api_base_url",
                "required_protocol_version",
                "ttl_seconds"
            })) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "unknown or invalid request fields"
                }
            }
        );

        return;
    }

    std::string node_id;
    std::string api_base_url;
    int required_protocol_version = 0;

    if (!json_string(
            body,
            "node_id",
            &node_id) ||
        !json_string(
            body,
            "api_base_url",
            &api_base_url) ||
        !json_int(
            body,
            "required_protocol_version",
            &required_protocol_version)) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "missing or invalid enrollment fields"
                }
            }
        );

        return;
    }

    std::uint64_t ttl_seconds =
        kAdminNodeEnrollmentDefaultTtlSeconds;

    if (body.contains("ttl_seconds") &&
        !json_u64(
            body,
            "ttl_seconds",
            &ttl_seconds)) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "invalid enrollment TTL"
                }
            }
        );

        return;
    }

    if (ttl_seconds <
            kAdminNodeEnrollmentMinimumTtlSeconds ||
        ttl_seconds >
            kAdminNodeEnrollmentMaximumTtlSeconds) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "enrollment TTL is outside the allowed range"
                },
                {
                    "minimum_ttl_seconds",
                    kAdminNodeEnrollmentMinimumTtlSeconds
                },
                {
                    "maximum_ttl_seconds",
                    kAdminNodeEnrollmentMaximumTtlSeconds
                }
            }
        );

        return;
    }

    std::string created_at;
    std::string expires_at;

    if (!create_time_window(
            context,
            ttl_seconds,
            &created_at,
            &expires_at)) {
        reply_json_local(
            context,
            res,
            503,
            json{
                {"ok", false},
                {"error", "clock_unavailable"},
                {
                    "message",
                    "enrollment time window unavailable"
                }
            }
        );

        return;
    }

    NodeEnrollmentCreateResult result =
        context.service->create_pending(
            node_id,
            api_base_url,
            required_protocol_version,
            created_at,
            expires_at,
            actor_fingerprint
        );

    if (!result.ok()) {
        reply_service_failure(
            context,
            res,
            result.result()
        );

        return;
    }

    std::string bootstrap_token;
    SensitiveStringWiper
        token_wiper(bootstrap_token);

    if (!result.take_bootstrap_token(
            &bootstrap_token) ||
        !valid_bootstrap_token_text(
            bootstrap_token)) {
        // Security: do not leave an unusable pending verifier if the
        // one-time secret cannot be transferred into the HTTP response.
        (void)context.service->revoke_pending(
            result.enrollment()
                .enrollment_id,
            result.enrollment()
                .generation,
            created_at,
            actor_fingerprint
        );

        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {
                    "error",
                    "enrollment_token_unavailable"
                },
                {
                    "message",
                    "bootstrap token unavailable"
                }
            }
        );

        return;
    }

    json response{
        {"ok", true},
        {
            "enrollment",
            enrollment_to_json(
                result.enrollment()
            )
        }
    };

    std::string response_body =
        response.dump();

    SensitiveStringWiper
        response_wiper(response_body);

    if (response_body.empty() ||
        response_body.back() != '}') {
        (void)context.service->revoke_pending(
            result.enrollment()
                .enrollment_id,
            result.enrollment()
                .generation,
            created_at,
            actor_fingerprint
        );

        reply_json_local(
            context,
            res,
            500,
            json{
                {"ok", false},
                {"error", "server_error"},
                {
                    "message",
                    "failed to serialize enrollment response"
                }
            }
        );

        return;
    }

    // The token alphabet is base64url without padding, so it requires no JSON
    // escaping. Building the final field directly avoids another JSON-owned
    // plaintext-token copy.
    response_body.pop_back();

    response_body.append(
        ",\"bootstrap_token\":\""
    );

    response_body.append(
        bootstrap_token
    );

    response_body.append("\"}");

    reply_json_text_local(
        context,
        res,
        201,
        response_body
    );
}

void handle_admin_node_enrollments_revoke(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminNodeEnrollmentRoutesContext&
        context
) {
    std::string actor_fingerprint;

    if (!authorize_mutation(
            context,
            req,
            res,
            &actor_fingerprint)) {
        return;
    }

    json body;

    if (!parse_json_object(
            context,
            req,
            res,
            &body)) {
        return;
    }

    if (!has_only_keys(
            body,
            {
                "enrollment_id",
                "generation"
            })) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "unknown or invalid request fields"
                }
            }
        );

        return;
    }

    std::string enrollment_id;
    std::uint64_t generation = 0;

    if (!json_string(
            body,
            "enrollment_id",
            &enrollment_id) ||
        !json_u64(
            body,
            "generation",
            &generation) ||
        generation == 0) {
        reply_json_local(
            context,
            res,
            400,
            json{
                {"ok", false},
                {"error", "bad_request"},
                {
                    "message",
                    "missing or invalid revocation fields"
                }
            }
        );

        return;
    }

    std::string revoked_at;

    if (!current_timestamp(
            context,
            &revoked_at)) {
        reply_json_local(
            context,
            res,
            503,
            json{
                {"ok", false},
                {"error", "clock_unavailable"},
                {
                    "message",
                    "revocation timestamp unavailable"
                }
            }
        );

        return;
    }

    const NodeEnrollmentMutationResult result =
        context.service->revoke_pending(
            enrollment_id,
            generation,
            revoked_at,
            actor_fingerprint
        );

    if (!result.ok) {
        reply_service_failure(
            context,
            res,
            result.result
        );

        return;
    }

    reply_json_local(
        context,
        res,
        200,
        json{
            {"ok", true},
            {
                "enrollment",
                enrollment_to_json(
                    result.enrollment
                )
            }
        }
    );
}

void register_admin_node_enrollment_routes(
    httplib::Server& server,
    const AdminNodeEnrollmentRoutesContext&
        context
) {
    const AdminNodeEnrollmentRoutesContext
        captured = context;

    server.Get(
        "/api/v4/admin/cluster/node-enrollments",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_node_enrollments_list(
                req,
                res,
                captured
            );
        }
    );

    server.Post(
        "/api/v4/admin/cluster/node-enrollments",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_node_enrollments_create(
                req,
                res,
                captured
            );
        }
    );

    server.Delete(
        "/api/v4/admin/cluster/node-enrollments",
        [captured](
            const httplib::Request& req,
            httplib::Response& res
        ) {
            handle_admin_node_enrollments_revoke(
                req,
                res,
                captured
            );
        }
    );
}

} // namespace pqnas::cluster
