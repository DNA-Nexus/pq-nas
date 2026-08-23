#include "routes/routes_node_enrollment_bootstrap.h"

#include "cluster/internal_auth.h"
#include "cluster/node_credential_registry.h"
#include "cluster/node_enrollment_consumption_service.h"
#include "cluster/node_enrollment_registry.h"
#include "cluster/node_enrollment_token.h"
#include "cluster/node_registry.h"
#include "httplib.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

using pqnas::cluster::
    NodeCredentialRegistry;
using pqnas::cluster::
    NodeCredentialRegistryResult;
using pqnas::cluster::
    NodeEnrollmentConsumptionService;
using pqnas::cluster::
    NodeEnrollmentConsumptionServiceConfig;
using pqnas::cluster::
    NodeEnrollmentProvisioningStage;
using pqnas::cluster::
    NodeEnrollmentRecord;
using pqnas::cluster::
    NodeEnrollmentRegistry;
using pqnas::cluster::
    NodeEnrollmentRegistryResult;
using pqnas::cluster::
    NodeEnrollmentState;
using pqnas::cluster::
    NodeEnrollmentBootstrapRoutesContext;
using pqnas::cluster::
    NodeRegistry;

int failures = 0;

struct Fixture {
    std::string enrollment_id;
    std::string node_id;
    std::string api_base_url;
    std::uint64_t generation = 0;
    std::string bootstrap_token;
    std::string node_credential;
};

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
            "pqnas-bootstrap-route-XXXXXX"
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

    return result
        ? fs::path(result)
        : fs::path{};
}

json response_json(
    const httplib::Response& response
) {
    try {
        return json::parse(
            response.body
        );
    } catch (...) {
        return json::object();
    }
}

httplib::Request request_with_body(
    const std::string& body,
    const std::string& bootstrap_token,
    const std::string& remote_addr =
        "bootstrap-client"
) {
    httplib::Request request;
    request.body = body;
    request.remote_addr = remote_addr;

    request.headers.emplace(
        "Content-Type",
        "application/json"
    );

    if (!bootstrap_token.empty()) {
        request.headers.emplace(
            "Authorization",
            "Bearer " +
                bootstrap_token
        );
    }

    return request;
}

std::string fixture_body(
    const Fixture& fixture
) {
    return json{
        {
            "enrollment_id",
            fixture.enrollment_id
        },
        {
            "expected_generation",
            fixture.generation
        },
        {
            "node_id",
            fixture.node_id
        },
        {
            "api_base_url",
            fixture.api_base_url
        },
        {
            "protocol_version",
            7
        },
        {
            "node_credential",
            fixture.node_credential
        }
    }.dump();
}

bool create_fixture(
    NodeEnrollmentRegistry* registry,
    char enrollment_character,
    const std::string& node_id,
    const std::string& api_base_url,
    const std::string& created_at,
    const std::string& expires_at,
    Fixture* fixture,
    std::string* error
) {
    if (!registry ||
        !fixture) {
        return false;
    }

    fixture->enrollment_id =
        std::string(
            32,
            enrollment_character
        );

    fixture->node_id =
        node_id;

    fixture->api_base_url =
        api_base_url;

    pqnas::cluster::
        NodeEnrollmentTokenMaterial material;

    if (pqnas::cluster::
            generate_node_enrollment_token(
                &material) !=
        pqnas::cluster::
            NodeEnrollmentTokenResult::Ok) {
        return false;
    }

    NodeEnrollmentRecord created;

    if (registry->create_or_replace_pending(
            fixture->enrollment_id,
            fixture->node_id,
            fixture->api_base_url,
            7,
            material.token_hash(),
            created_at,
            expires_at,
            &created,
            error) !=
        NodeEnrollmentRegistryResult::Ok) {
        return false;
    }

    fixture->generation =
        created.generation;

    if (!material.take_token(
            &fixture->bootstrap_token)) {
        return false;
    }

    return
        pqnas::cluster::
            generate_internal_auth_credential_token(
                &fixture->node_credential,
                error
            );
}

bool prepare_fixture(
    NodeEnrollmentRegistry* registry,
    const Fixture& fixture,
    const std::string& started_at,
    NodeEnrollmentRecord* output,
    std::string* error
) {
    std::string token_hash;

    const auto hash_result =
        pqnas::cluster::
            hash_node_enrollment_token(
                fixture.bootstrap_token,
                &token_hash
            );

    const auto result =
        hash_result ==
            pqnas::cluster::
                NodeEnrollmentTokenResult::Ok
        ? registry->
            prepare_provisioning_if_matches(
                fixture.enrollment_id,
                fixture.node_id,
                fixture.api_base_url,
                7,
                fixture.generation,
                token_hash,
                started_at,
                output,
                error
            )
        : NodeEnrollmentRegistryResult::
            TokenInvalid;

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &token_hash
        );

    return result ==
        NodeEnrollmentRegistryResult::Ok;
}

void clear_fixture(
    Fixture* fixture
) {
    if (!fixture) {
        return;
    }

    pqnas::cluster::
        clear_node_enrollment_token_text(
            &fixture->bootstrap_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &fixture->node_credential
        );
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";
        return 1;
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
        fs::remove_all(
            directory,
            error
        );
    };

    NodeEnrollmentRegistry
        enrollment_registry;

    NodeCredentialRegistry
        credential_registry;

    NodeRegistry node_registry;

    std::string error;

    expect(
        enrollment_registry.load(
            (
                directory /
                "enrollments.json"
            ).string(),
            &error
        ),
        "load route enrollment registry: " +
            error
    );

    error.clear();

    expect(
        credential_registry.load(
            (
                directory /
                "credentials"
            ).string(),
            &error
        ),
        "load route credential registry: " +
            error
    );

    error.clear();

    expect(
        node_registry.load(
            (
                directory /
                "nodes.json"
            ).string(),
            &error
        ),
        "load route node registry: " +
            error
    );

    NodeEnrollmentConsumptionServiceConfig
        service_config;

    service_config.enrollment_registry =
        &enrollment_registry;

    service_config.credential_registry =
        &credential_registry;

    service_config.node_registry =
        &node_registry;

    service_config.audit_emit =
        [](
            const std::string&,
            const std::string&,
            const std::map<
                std::string,
                std::string
            >&
        ) {};

    NodeEnrollmentConsumptionService service(
        service_config
    );

    std::string now_iso =
        "2026-07-31T06:00:00Z";

    std::int64_t now_epoch = 1000;

    NodeEnrollmentBootstrapRoutesContext context;
    context.service = &service;

    context.lifecycle_mutex =
        std::make_shared<std::mutex>();

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
            return now_iso;
        };

    context.now_epoch_sec =
        [&]() {
            return now_epoch;
        };

    {
        NodeEnrollmentBootstrapRoutesContext
            unavailable_context;

        httplib::Request request;
        request.remote_addr =
            "unavailable-client";

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                unavailable_context
            );

        expect(
            response.status == 503 &&
            response_json(response)["error"] ==
                "enrollment_consumption_service_unavailable",
            "unavailable bootstrap route"
        );
    }

    Fixture fresh;

    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'A',
            "node-bootstrap-fresh",
            "https://node-bootstrap-fresh.internal/",
            "2026-07-31T05:00:00Z",
            "2026-07-31T07:00:00Z",
            &fresh,
            &error
        ),
        "create fresh route fixture: " +
            error
    );

    {
        httplib::Request request =
            request_with_body(
                fixture_body(fresh),
                "",
                "missing-auth-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 401 &&
            response_json(response)["error"] ==
                "enrollment_auth_failed",
            "missing bootstrap Authorization"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                fixture_body(fresh),
                fresh.bootstrap_token,
                "duplicate-auth-client"
            );

        request.headers.emplace(
            "Authorization",
            "Bearer " +
                fresh.bootstrap_token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "duplicate Authorization rejected"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                fixture_body(fresh),
                fresh.bootstrap_token,
                "content-type-client"
            );

        request.headers.erase(
            "Content-Type"
        );

        request.headers.emplace(
            "Content-Type",
            "text/plain"
        );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 415,
            "non-JSON bootstrap content type"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                std::string(
                    pqnas::cluster::
                        kNodeEnrollmentBootstrapBodyMaxBytes +
                    1,
                    'X'
                ),
                fresh.bootstrap_token,
                "oversized-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized bootstrap body"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                "{",
                fresh.bootstrap_token,
                "bad-json-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 400 &&
            response_json(response)["error"] ==
                "bad_json",
            "malformed bootstrap JSON"
        );
    }

    {
        json body =
            json::parse(
                fixture_body(fresh)
            );

        body["unexpected"] = true;

        httplib::Request request =
            request_with_body(
                body.dump(),
                fresh.bootstrap_token,
                "unknown-field-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "unknown bootstrap JSON field"
        );
    }

    {
        json body =
            json::parse(
                fixture_body(fresh)
            );

        body["expected_generation"] =
            "one";

        httplib::Request request =
            request_with_body(
                body.dump(),
                fresh.bootstrap_token,
                "wrong-type-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "bootstrap field type mismatch"
        );
    }

    {
        // Regression: production runtime timestamps may contain
        // milliseconds; canonicalize before the strict consumption service.
        now_iso =
            "2026-07-31T06:00:00.123Z";

        const std::string request_text =
            fixture_body(fresh);

        httplib::Request request =
            request_with_body(
                request_text,
                fresh.bootstrap_token,
                "fresh-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        const json response_body =
            response_json(response);

        expect(
            response.status == 200 &&
            response_body["ok"] == true &&
            response_body["recovered"] ==
                false,
            "successful fresh bootstrap route"
        );

        expect(
            response.body.find(
                fresh.bootstrap_token) ==
                    std::string::npos &&
            response.body.find(
                fresh.node_credential) ==
                    std::string::npos &&
            response.body.find(
                fresh.api_base_url) ==
                    std::string::npos,
            "bootstrap response excludes secrets and URL"
        );

        const auto record =
            enrollment_registry.get(
                fresh.enrollment_id
            );

        expect(
            record.has_value() &&
            record->state ==
                NodeEnrollmentState::Consumed &&
            record->token_hash.empty() &&
            record->consumed_at ==
                "2026-07-31T06:00:00Z",
            "fresh route canonicalizes runtime timestamp and finalizes enrollment"
        );

        bool matched = false;
        error.clear();

        expect(
            credential_registry.matches(
                fresh.node_id,
                fresh.node_credential,
                &matched,
                &error
            ) ==
                NodeCredentialRegistryResult::Ok &&
            matched,
            "fresh route persists credential"
        );

        const auto node =
            node_registry.get(
                fresh.node_id
            );

        expect(
            node.has_value() &&
            node->api_base_url ==
                "https://node-bootstrap-fresh.internal",
            "fresh route registers canonical node"
        );
    }

    {
        httplib::Request request =
            request_with_body(
                fixture_body(fresh),
                fresh.node_credential,
                "repeat-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        const json response_body =
            response_json(response);

        expect(
            response.status == 200 &&
            response_body["ok"] == true &&
            response_body["recovered"] ==
                true,
            "finalized enrollment recovers with persisted credential"
        );

        expect(
            response.body.find(
                fresh.node_credential) ==
                    std::string::npos &&
            response.body.find(
                fresh.api_base_url) ==
                    std::string::npos,
            "finalized recovery response excludes credential and URL"
        );
    }

    Fixture wrong_token;

    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'B',
            "node-bootstrap-wrong-token",
            "https://node-bootstrap-wrong-token.internal",
            "2026-07-31T05:00:00Z",
            "2026-07-31T07:00:00Z",
            &wrong_token,
            &error
        ),
        "create wrong-token route fixture: " +
            error
    );

    {
        httplib::Request request =
            request_with_body(
                fixture_body(wrong_token),
                fresh.bootstrap_token,
                "wrong-token-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 401 &&
            response_json(response)["error"] ==
                "enrollment_auth_failed",
            "wrong bootstrap token uses generic failure"
        );

        const auto record =
            enrollment_registry.get(
                wrong_token.enrollment_id
            );

        expect(
            record.has_value() &&
            record->state ==
                NodeEnrollmentState::Pending &&
            record->provisioning_stage ==
                NodeEnrollmentProvisioningStage::
                    None,
            "wrong token must not mutate enrollment"
        );
    }

    Fixture recovery;

    error.clear();

    expect(
        create_fixture(
            &enrollment_registry,
            'C',
            "node-bootstrap-recovery",
            "https://node-bootstrap-recovery.internal",
            "2026-07-31T05:00:00Z",
            "2026-07-31T05:30:00Z",
            &recovery,
            &error
        ),
        "create bootstrap recovery fixture: " +
            error
    );

    NodeEnrollmentRecord transition_record;

    error.clear();

    expect(
        prepare_fixture(
            &enrollment_registry,
            recovery,
            "2026-07-31T05:20:00Z",
            &transition_record,
            &error
        ),
        "prepare bootstrap recovery fixture: " +
            error
    );

    error.clear();

    expect(
        credential_registry.create_if_absent(
            recovery.node_id,
            recovery.node_credential,
            &error
        ) ==
            NodeCredentialRegistryResult::Ok,
        "persist bootstrap recovery credential: " +
            error
    );

    now_iso =
        "2026-07-31T06:30:00Z";

    {
        httplib::Request request =
            request_with_body(
                fixture_body(recovery),
                recovery.bootstrap_token,
                "recovery-client"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 200 &&
            response_json(response)["recovered"] ==
                true,
            "bootstrap route recovers after TTL"
        );
    }

    {
        for (int index = 0;
             index <
                 pqnas::cluster::
                     kNodeEnrollmentBootstrapRateLimit;
             ++index) {
            httplib::Request request;
            request.remote_addr =
                "rate-client";

            httplib::Response response;

            pqnas::cluster::
                handle_node_enrollment_bootstrap_consume(
                    request,
                    response,
                    context
                );

            expect(
                response.status == 401,
                "pre-limit bootstrap request"
            );
        }

        httplib::Request request;
        request.remote_addr =
            "rate-client";

        httplib::Response response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                response,
                context
            );

        expect(
            response.status == 429 &&
            response_json(response)["error"] ==
                "rate_limited",
            "bootstrap rate limit"
        );

        now_epoch +=
            pqnas::cluster::
                kNodeEnrollmentBootstrapRateWindowSeconds +
            1;

        httplib::Response reset_response;

        pqnas::cluster::
            handle_node_enrollment_bootstrap_consume(
                request,
                reset_response,
                context
            );

        expect(
            reset_response.status == 401,
            "bootstrap rate window reset"
        );
    }

    clear_fixture(&fresh);
    clear_fixture(&wrong_token);
    clear_fixture(&recovery);

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " bootstrap route test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node enrollment bootstrap route tests passed\n";

    return 0;
}
