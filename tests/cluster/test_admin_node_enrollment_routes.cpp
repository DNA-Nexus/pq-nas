#include "routes/routes_admin_node_enrollments.h"

#include "cluster/node_enrollment_registry.h"
#include "cluster/node_enrollment_service.h"
#include "cluster/node_enrollment_token.h"
#include "httplib.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-admin-enrollment-route-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(
            writable.data()
        );

    if (!result) {
        return {};
    }

    return fs::path(result);
}

httplib::Request request_with_body(
    std::string body
) {
    httplib::Request request;

    request.body = std::move(body);
    request.remote_addr = "192.0.2.20";

    return request;
}

json response_json(
    const httplib::Response& response
) {
    try {
        return json::parse(
            response.body
        );
    } catch (...) {
        return json();
    }
}

std::string response_header(
    const httplib::Response& response,
    const std::string& name
) {
    const auto iterator =
        response.headers.find(name);

    if (iterator ==
        response.headers.end()) {
        return {};
    }

    return iterator->second;
}

bool base64url_token(
    const std::string& value
) {
    if (value.size() !=
        pqnas::cluster::
            kNodeEnrollmentTokenChars) {
        return false;
    }

    for (unsigned char ch : value) {
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

void clear_secret(
    std::string* value
) {
    pqnas::cluster::
        clear_node_enrollment_token_text(
            value
        );
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";

        return 1;
    }

    {
        std::string added =
            "previous-output";

        expect(
            pqnas::cluster::
                add_admin_node_enrollment_seconds_iso_utc(
                    "2026-07-30T23:59:30Z",
                    90,
                    &added
                ) &&
            added ==
                "2026-07-31T00:01:00Z",
            "enrollment timestamp day rollover"
        );

        expect(
            pqnas::cluster::
                add_admin_node_enrollment_seconds_iso_utc(
                    "2028-02-29T23:59:30Z",
                    60,
                    &added
                ) &&
            added ==
                "2028-03-01T00:00:30Z",
            "enrollment timestamp leap-day rollover"
        );

        added = "must-be-cleared";

        expect(
            !pqnas::cluster::
                add_admin_node_enrollment_seconds_iso_utc(
                    "2026-02-30T12:00:00Z",
                    60,
                    &added
                ) &&
            added.empty(),
            "invalid calendar date must be rejected"
        );

        expect(
            !pqnas::cluster::
                add_admin_node_enrollment_seconds_iso_utc(
                    "2026-07-30 12:00:00Z",
                    60,
                    &added
                ),
            "noncanonical enrollment timestamp"
        );

        expect(
            !pqnas::cluster::
                add_admin_node_enrollment_seconds_iso_utc(
                    "2026-07-30T12:00:00Z",
                    pqnas::cluster::
                        kAdminNodeEnrollmentMaximumTtlSeconds +
                    1,
                    &added
                ),
            "oversized enrollment timestamp addition"
        );
    }

    const fs::path directory =
        make_test_directory();

    if (directory.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(directory, error);
    };

    pqnas::cluster::NodeEnrollmentRegistry
        registry;

    std::string registry_error;

    expect(
        registry.load(
            (
                directory /
                "node_enrollments.json"
            ).string(),
            &registry_error
        ),
        "load enrollment registry: " +
            registry_error
    );

    std::vector<std::string> ids{
        std::string(32, 'A'),
        std::string(32, 'B'),
        std::string(32, 'C'),
        std::string(32, 'D')
    };

    std::size_t id_index = 0;

    pqnas::cluster::
        NodeEnrollmentServiceConfig
            service_config;

    service_config.registry = &registry;

    service_config.generate_enrollment_id =
        [&](std::string* output) {
            if (!output ||
                id_index >= ids.size()) {
                return false;
            }

            *output =
                ids[id_index++];

            return true;
        };

    service_config.audit_emit =
        [](
            const std::string&,
            const std::string&,
            const std::map<
                std::string,
                std::string
            >&) {};

    pqnas::cluster::NodeEnrollmentService
        service(service_config);

    bool auth_allowed = true;
    bool origin_allowed = true;

    int auth_calls = 0;
    int origin_calls = 0;

    std::string now_value =
        "2026-07-30T12:00:00Z";

    std::string expiry_value =
        "2026-07-30T12:15:00Z";

    pqnas::cluster::
        AdminNodeEnrollmentRoutesContext
            context;

    context.service = &service;

    context.lifecycle_mutex =
        std::make_shared<std::mutex>();

    context.require_admin_auth =
        [&](
            const httplib::Request&,
            httplib::Response& response,
            std::string* actor_fingerprint
        ) {
            ++auth_calls;

            if (!auth_allowed) {
                response.status = 401;

                response.set_content(
                    R"({"ok":false,"error":"unauthorized"})",
                    "application/json"
                );

                return false;
            }

            if (actor_fingerprint) {
                *actor_fingerprint =
                    "admin-fingerprint";
            }

            return true;
        };

    context.require_same_origin =
        [&](
            const httplib::Request&,
            httplib::Response& response
        ) {
            ++origin_calls;

            if (!origin_allowed) {
                response.status = 403;

                response.set_content(
                    R"({"ok":false,"error":"forbidden"})",
                    "application/json"
                );

                return false;
            }

            return true;
        };

    context.reply_json =
        [](
            httplib::Response& response,
            int status,
            const std::string& body
        ) {
            response.status = status;

            response.set_content(
                body,
                "application/json; charset=utf-8"
            );
        };

    context.now_iso_utc =
        [&]() {
            // Regression: the shared runtime clock includes milliseconds.
            if (now_value.size() != 20 ||
                now_value.back() != 'Z') {
                return now_value;
            }

            return
                now_value.substr(0, 19) +
                ".123Z";
        };

    context.add_seconds_iso_utc =
        [&](
            const std::string& base,
            std::uint64_t seconds,
            std::string* output
        ) {
            if (!output ||
                base != now_value ||
                seconds <
                    pqnas::cluster::
                        kAdminNodeEnrollmentMinimumTtlSeconds ||
                seconds >
                    pqnas::cluster::
                        kAdminNodeEnrollmentMaximumTtlSeconds) {
                return false;
            }

            *output = expiry_value;
            return true;
        };

    // Authentication must happen before same-origin and before body parsing.
    {
        auth_allowed = false;
        origin_allowed = true;
        auth_calls = 0;
        origin_calls = 0;

        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminNodeEnrollmentRequestBodyMaxBytes +
                    100,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "authentication should run before enrollment body parsing"
        );

        expect(
            auth_calls == 1 &&
            origin_calls == 0,
            "unauthorized create must not reach same-origin check"
        );

        expect(
            registry.snapshot().empty(),
            "unauthorized create must not mutate enrollment registry"
        );
    }

    // GET requires admin authentication but is not a cookie mutation.
    {
        auth_allowed = true;
        origin_allowed = false;
        auth_calls = 0;
        origin_calls = 0;

        httplib::Request request;
        request.remote_addr = "192.0.2.20";

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_list(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["enrollments"].is_array() &&
            body["enrollments"].empty(),
            "empty enrollment list"
        );

        expect(
            auth_calls == 1 &&
            origin_calls == 0,
            "enrollment GET must not require same-origin"
        );

        expect(
            response_header(
                response,
                "Cache-Control"
            ) == "no-store",
            "enrollment GET must disable caching"
        );
    }

    // Same-origin rejection must happen before parsing or service mutation.
    {
        auth_allowed = true;
        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-route",
                    "api_base_url":"https://node-route.internal",
                    "required_protocol_version":1
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "same-origin failure should block enrollment create"
        );

        expect(
            registry.snapshot().empty(),
            "same-origin failure must not mutate enrollment registry"
        );
    }

    auth_allowed = true;
    origin_allowed = true;

    {
        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kAdminNodeEnrollmentRequestBodyMaxBytes +
                    1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized enrollment body"
        );

        expect(
            registry.snapshot().empty(),
            "oversized body must not mutate registry"
        );
    }

    {
        httplib::Request request =
            request_with_body("{");

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        expect(
            response.status == 400 &&
            response_json(response)["error"] ==
                "bad_json",
            "malformed enrollment JSON"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-route",
                    "api_base_url":"https://node-route.internal",
                    "required_protocol_version":1,
                    "unexpected":true
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "unknown enrollment field must be rejected"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-route",
                    "api_base_url":"https://node-route.internal",
                    "required_protocol_version":1,
                    "ttl_seconds":59
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "short enrollment TTL must be rejected"
        );
    }

    std::string bootstrap_token;
    std::string enrollment_id;
    std::uint64_t generation = 0;

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-route",
                    "api_base_url":"https://node-route.internal/",
                    "required_protocol_version":1
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 201 &&
            body["ok"] == true,
            "valid enrollment create"
        );

        expect(
            body["enrollment"]["node_id"] ==
                "node-route" &&
            body["enrollment"]["api_base_url"] ==
                "https://node-route.internal" &&
            body["enrollment"]["state"] ==
                "pending" &&
            body["enrollment"]["created_at"] ==
                now_value &&
            body["enrollment"]["expires_at"] ==
                expiry_value,
            "created enrollment response metadata"
        );

        expect(
            body.contains("bootstrap_token") &&
            body["bootstrap_token"].is_string(),
            "create response must contain one-time token"
        );

        bootstrap_token =
            body["bootstrap_token"]
                .get<std::string>();

        expect(
            base64url_token(
                bootstrap_token
            ),
            "bootstrap token response format"
        );

        expect(
            response.body.find(
                "token_hash"
            ) == std::string::npos,
            "create response must never expose verifier"
        );

        enrollment_id =
            body["enrollment"]
                ["enrollment_id"]
                .get<std::string>();

        generation =
            body["enrollment"]
                ["generation"]
                .get<std::uint64_t>();

        const auto persisted =
            registry.get(enrollment_id);

        std::string recomputed_hash;

        expect(
            pqnas::cluster::
                hash_node_enrollment_token(
                    bootstrap_token,
                    &recomputed_hash
                ) ==
                    pqnas::cluster::
                        NodeEnrollmentTokenResult::Ok &&
            persisted.has_value() &&
            persisted->token_hash ==
                recomputed_hash,
            "HTTP token must match persisted verifier"
        );

        clear_secret(&recomputed_hash);

        expect(
            response_header(
                response,
                "Cache-Control"
            ) == "no-store",
            "token response must disable caching"
        );
    }

    clear_secret(&bootstrap_token);

    {
        httplib::Request request;
        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_list(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 200 &&
            body["count"] == 1 &&
            body["enrollments"].size() == 1,
            "list pending enrollment"
        );

        expect(
            !body["enrollments"][0].contains(
                "bootstrap_token"
            ) &&
            !body["enrollments"][0].contains(
                "token_hash"
            ) &&
            response.body.find(
                "token_hash"
            ) == std::string::npos,
            "list response must remain redacted"
        );
    }

    // A second node may not claim the active URL.
    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-other",
                    "api_base_url":"https://node-route.internal",
                    "required_protocol_version":1
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 409 &&
            body["error"] ==
                "enrollment_url_conflict",
            "active enrollment URL conflict"
        );

        expect(
            !body.contains(
                "bootstrap_token"
            ),
            "failed create must not expose token"
        );
    }

    // Same-origin must also protect revocation.
    {
        origin_allowed = false;

        httplib::Request request =
            request_with_body(
                json{
                    {
                        "enrollment_id",
                        enrollment_id
                    },
                    {
                        "generation",
                        generation
                    }
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_revoke(
                request,
                response,
                context
            );

        const auto persisted =
            registry.get(enrollment_id);

        expect(
            response.status == 403 &&
            persisted.has_value() &&
            persisted->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Pending &&
            !persisted->token_hash.empty(),
            "same-origin failure must preserve pending verifier"
        );

        origin_allowed = true;
    }

    {
        httplib::Request request =
            request_with_body(
                json{
                    {
                        "enrollment_id",
                        enrollment_id
                    },
                    {
                        "generation",
                        generation + 1
                    }
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_revoke(
                request,
                response,
                context
            );

        const auto persisted =
            registry.get(enrollment_id);

        expect(
            response.status == 409 &&
            response_json(response)["error"] ==
                "enrollment_generation_mismatch",
            "stale revocation generation"
        );

        expect(
            persisted.has_value() &&
            persisted->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Pending &&
            !persisted->token_hash.empty(),
            "stale revoke must preserve verifier"
        );
    }

    now_value =
        "2026-07-30T12:05:00Z";

    {
        httplib::Request request =
            request_with_body(
                json{
                    {
                        "enrollment_id",
                        enrollment_id
                    },
                    {
                        "generation",
                        generation
                    }
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_revoke(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        const auto persisted =
            registry.get(enrollment_id);

        expect(
            response.status == 200 &&
            body["enrollment"]["state"] ==
                "revoked" &&
            body["enrollment"]["revoked_at"] ==
                now_value,
            "successful enrollment revocation"
        );

        expect(
            persisted.has_value() &&
            persisted->state ==
                pqnas::cluster::
                    NodeEnrollmentState::Revoked &&
            persisted->token_hash.empty(),
            "successful route revoke clears verifier"
        );

        expect(
            response.body.find(
                "bootstrap_token"
            ) == std::string::npos &&
            response.body.find(
                "token_hash"
            ) == std::string::npos,
            "revoke response must contain no secret material"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                json{
                    {
                        "enrollment_id",
                        std::string(32, 'Z')
                    },
                    {"generation", 1}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_revoke(
                request,
                response,
                context
            );

        expect(
            response.status == 404 &&
            response_json(response)["error"] ==
                "enrollment_not_found",
            "missing enrollment revocation"
        );
    }

    // Verify the consumed-state HTTP mapping.
    now_value =
        "2026-07-30T13:00:00Z";

    expiry_value =
        "2026-07-30T13:15:00Z";

    std::string consumed_id;
    std::uint64_t consumed_generation = 0;

    {
        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-consumed-route",
                    "api_base_url":"https://node-consumed-route.internal",
                    "required_protocol_version":1
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        const json body =
            response_json(response);

        expect(
            response.status == 201,
            "create consumed route fixture"
        );

        std::string plaintext =
            body["bootstrap_token"]
                .get<std::string>();

        std::string token_hash;

        expect(
            pqnas::cluster::
                hash_node_enrollment_token(
                    plaintext,
                    &token_hash
                ) ==
                    pqnas::cluster::
                        NodeEnrollmentTokenResult::Ok,
            "hash consumed route fixture"
        );

        clear_secret(&plaintext);

        consumed_id =
            body["enrollment"]
                ["enrollment_id"]
                .get<std::string>();

        consumed_generation =
            body["enrollment"]
                ["generation"]
                .get<std::uint64_t>();

        pqnas::cluster::NodeEnrollmentRecord
            consumed;

        registry_error.clear();

        expect(
            registry.prepare_provisioning_if_matches(
                consumed_id,
                "node-consumed-route",
                "https://node-consumed-route.internal",
                1,
                consumed_generation,
                token_hash,
                "2026-07-30T13:05:00Z",
                &consumed,
                &registry_error
            ) ==
                pqnas::cluster::
                    NodeEnrollmentRegistryResult::Ok,
            "prepare consumed route fixture: " +
                registry_error
        );

        expect(
            registry.advance_provisioning_stage(
                consumed_id,
                consumed_generation,
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        Prepared,
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        CredentialPersisted,
                &consumed,
                &registry_error
            ) ==
                pqnas::cluster::
                    NodeEnrollmentRegistryResult::Ok,
            "advance consumed route credential fixture: " +
                registry_error
        );

        expect(
            registry.advance_provisioning_stage(
                consumed_id,
                consumed_generation,
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        CredentialPersisted,
                pqnas::cluster::
                    NodeEnrollmentProvisioningStage::
                        NodeRegistered,
                &consumed,
                &registry_error
            ) ==
                pqnas::cluster::
                    NodeEnrollmentRegistryResult::Ok,
            "advance consumed route node fixture: " +
                registry_error
        );

        expect(
            registry.finalize_provisioning(
                consumed_id,
                consumed_generation,
                "2026-07-30T13:05:00Z",
                &consumed,
                &registry_error
            ) ==
                pqnas::cluster::
                    NodeEnrollmentRegistryResult::Ok,
            "finalize consumed route fixture: " +
                registry_error
        );

        clear_secret(&token_hash);
    }

    {
        now_value =
            "2026-07-30T13:06:00Z";

        httplib::Request request =
            request_with_body(
                json{
                    {
                        "enrollment_id",
                        consumed_id
                    },
                    {
                        "generation",
                        consumed_generation
                    }
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_revoke(
                request,
                response,
                context
            );

        expect(
            response.status == 409 &&
            response_json(response)["error"] ==
                "enrollment_consumed",
            "consumed enrollment HTTP conflict"
        );
    }

    // Clock failure must occur before creating a new service mutation.
    {
        const std::size_t before =
            registry.snapshot().size();

        now_value.clear();

        httplib::Request request =
            request_with_body(
                R"({
                    "node_id":"node-clock",
                    "api_base_url":"https://node-clock.internal",
                    "required_protocol_version":1
                })"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_create(
                request,
                response,
                context
            );

        expect(
            response.status == 503 &&
            response_json(response)["error"] ==
                "clock_unavailable",
            "clock failure response"
        );

        expect(
            registry.snapshot().size() ==
                before,
            "clock failure must not mutate registry"
        );
    }

    // A missing service must fail closed without calling authentication.
    {
        pqnas::cluster::
            AdminNodeEnrollmentRoutesContext
                unavailable_context =
                    context;

        unavailable_context.service =
            nullptr;

        auth_calls = 0;

        httplib::Request request;
        httplib::Response response;

        pqnas::cluster::
            handle_admin_node_enrollments_list(
                request,
                response,
                unavailable_context
            );

        expect(
            response.status == 503 &&
            response_json(response)["error"] ==
                "service_unavailable",
            "missing enrollment route service"
        );

        expect(
            auth_calls == 0,
            "invalid route context must fail before auth callback"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " admin node enrollment route test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: admin node enrollment route tests passed\n";

    return 0;
}
