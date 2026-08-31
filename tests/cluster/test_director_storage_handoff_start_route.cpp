#include "routes/routes_director_storage_handoff_start.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

using json = nlohmann::json;
using namespace pqnas::cluster;

const std::string kFingerprint(128, 'a');
const std::string kBindingHash(43, 'A');
const std::string kTicket(43, 'A');

int failures = 0;


void check(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        std::cerr
            << "FAIL: "
            << message
            << '\n';

        ++failures;
    }
}


DirectorStorageHandoffTarget
good_target() {
    DirectorStorageHandoffTarget target;

    target.home_node =
        "node-01";

    target.projection_revision = 7;

    target.api_base_url =
        "https://storage-internal.example.test";

    target.browser_base_url =
        "https://storage.example.test";

    return target;
}


DirectorStorageHandoffStartRoutesContext
good_context() {
    DirectorStorageHandoffStartRoutesContext
        context;

    context.expected_director_origin =
        "https://director.example.test";

    context.require_user_cookie =
        [](
            const httplib::Request&,
            httplib::Response&,
            std::string* fingerprint,
            std::string* role
        ) {
            *fingerprint = kFingerprint;
            *role = "user";
            return true;
        };

    context.consume_rate_limit_slot =
        [](
            const std::string&,
            long* retry_after
        ) {
            if (retry_after) {
                *retry_after = 0;
            }

            return true;
        };

    context.resolve_authoritative_target =
        [](
            const std::string&
        ) {
            DirectorStorageHandoffTargetResult
                result;

            result.code =
                DirectorStorageHandoffTargetCode::Ok;

            result.target =
                good_target();

            return result;
        };

    context.issue_ticket =
        [](
            const std::string&,
            const std::string&,
            const std::string&
        ) {
            StorageHandoffTicketClientResult
                result;

            result.code =
                StorageHandoffTicketClientCode::Ok;

            result.http_status = 200;
            result.ticket = kTicket;
            result.expires_at = 2000000000;

            return result;
        };

    return context;
}


httplib::Request
good_request() {
    httplib::Request request;

    request.headers.emplace(
        "Origin",
        "https://director.example.test"
    );

    request.headers.emplace(
        "Content-Type",
        "application/json"
    );

    request.body =
        json{
            {
                "browser_binding_hash",
                kBindingHash
            }
        }.dump();

    return request;
}


httplib::Request
good_begin_request() {
    httplib::Request request;

    request.headers.emplace(
        "Origin",
        "https://director.example.test"
    );

    return request;
}


json response_json(
    const httplib::Response& response
) {
    return json::parse(
        response.body
    );
}



void test_begin_returns_only_trusted_browser_origin() {
    auto context = good_context();
    auto request = good_begin_request();

    int resolve_calls = 0;
    int issue_calls = 0;

    context.resolve_authoritative_target =
        [&](
            const std::string&
        ) {
            ++resolve_calls;

            DirectorStorageHandoffTargetResult
                result;

            result.code =
                DirectorStorageHandoffTargetCode::Ok;

            result.target =
                good_target();

            return result;
        };

    context.issue_ticket =
        [&](
            const std::string&,
            const std::string&,
            const std::string&
        ) {
            ++issue_calls;

            StorageHandoffTicketClientResult
                result;

            result.code =
                StorageHandoffTicketClientCode::Ok;

            return result;
        };

    httplib::Response response;

    handle_director_storage_handoff_begin_request(
        request,
        response,
        context
    );

    const json body =
        response_json(response);

    check(
        response.status == 200 &&
        body.is_object() &&
        body.size() == 2 &&
        body.value("ok", false) &&
        body.value(
            "storage_origin",
            ""
        ) ==
            "https://storage.example.test",
        "begin must return only the trusted Storage browser origin"
    );

    check(
        resolve_calls == 1,
        "begin must resolve current authoritative target exactly once"
    );

    check(
        issue_calls == 0,
        "begin must never mint a Storage handoff ticket"
    );

    check(
        response.body.find(
            "storage-internal.example.test"
        ) == std::string::npos &&
        response.body.find(
            "node-01"
        ) == std::string::npos &&
        response.body.find(
            kFingerprint
        ) == std::string::npos &&
        response.body.find(
            "projection_revision"
        ) == std::string::npos,
        "begin response must not expose internal routing or user authority"
    );
}


void test_begin_security_gates() {
    {
        auto context = good_context();
        auto request = good_begin_request();

        bool auth_called = false;

        request.headers.emplace(
            "Authorization",
            "Bearer forbidden"
        );

        context.require_user_cookie =
            [&](
                const httplib::Request&,
                httplib::Response&,
                std::string*,
                std::string*
            ) {
                auth_called = true;
                return true;
            };

        httplib::Response response;

        handle_director_storage_handoff_begin_request(
            request,
            response,
            context
        );

        check(
            response.status == 403 &&
            !auth_called,
            "begin must reject Authorization before cookie authentication"
        );
    }

    {
        auto context = good_context();
        auto request = good_begin_request();

        request.body =
            R"({"node":"attacker-choice"})";

        int resolve_calls = 0;

        context.resolve_authoritative_target =
            [&](
                const std::string&
            ) {
                ++resolve_calls;

                DirectorStorageHandoffTargetResult
                    result;

                result.code =
                    DirectorStorageHandoffTargetCode::Ok;

                result.target =
                    good_target();

                return result;
            };

        httplib::Response response;

        handle_director_storage_handoff_begin_request(
            request,
            response,
            context
        );

        check(
            response.status == 400 &&
            resolve_calls == 0,
            "begin must reject every browser-supplied request payload"
        );
    }

    {
        auto context = good_context();
        auto request = good_begin_request();

        context.resolve_authoritative_target =
            [](
                const std::string&
            ) {
                DirectorStorageHandoffTargetResult
                    result;

                result.code =
                    DirectorStorageHandoffTargetCode::
                        UserNotAuthorized;

                return result;
            };

        httplib::Response response;

        handle_director_storage_handoff_begin_request(
            request,
            response,
            context
        );

        check(
            response.status == 403,
            "begin must preserve user authorization failure"
        );
    }

    {
        auto context = good_context();
        auto request = good_begin_request();

        context.resolve_authoritative_target =
            [](
                const std::string&
            ) {
                DirectorStorageHandoffTargetResult
                    result;

                result.code =
                    DirectorStorageHandoffTargetCode::Ok;

                result.target =
                    good_target();

                result.target.browser_base_url =
                    "https://storage.example.test/";

                return result;
            };

        httplib::Response response;

        handle_director_storage_handoff_begin_request(
            request,
            response,
            context
        );

        check(
            response.status == 503,
            "begin must fail closed on noncanonical Storage browser authority"
        );
    }
}


void test_success_uses_authoritative_target() {
    auto context = good_context();
    auto request = good_request();

    std::string issued_node;
    std::string issued_fp;
    std::string issued_binding;

    context.issue_ticket =
        [&](
            const std::string& node,
            const std::string& fp,
            const std::string& binding
        ) {
            issued_node = node;
            issued_fp = fp;
            issued_binding = binding;

            StorageHandoffTicketClientResult
                result;

            result.code =
                StorageHandoffTicketClientCode::Ok;

            result.http_status = 200;
            result.ticket = kTicket;
            result.expires_at = 2000000000;

            return result;
        };

    httplib::Response response;

    handle_director_storage_handoff_start_request(
        request,
        response,
        context
    );

    check(
        response.status == 200,
        "valid handoff start must succeed"
    );

    const auto body =
        response_json(response);

    check(
        body["ok"] == true,
        "success body must report ok"
    );

    check(
        body["storage_origin"] ==
            "https://storage.example.test",
        "success must return trusted Storage browser origin"
    );

    check(
        body["ticket"] == kTicket,
        "success must return ticket"
    );

    check(
        issued_node == "node-01" &&
        issued_fp == kFingerprint &&
        issued_binding == kBindingHash,
        "ticket issue must use authoritative target, actor and binding"
    );

    check(
        response.get_header_value(
            "Cache-Control") == "no-store",
        "handoff response must be non-cacheable"
    );
}


void test_noncanonical_director_origin_fails_closed() {
    auto context = good_context();
    auto request = good_request();

    context.expected_director_origin =
        "https://director.example.test/";

    bool auth_called = false;

    context.require_user_cookie =
        [&](
            const httplib::Request&,
            httplib::Response&,
            std::string*,
            std::string*
        ) {
            auth_called = true;
            return true;
        };

    httplib::Response response;

    handle_director_storage_handoff_start_request(
        request,
        response,
        context
    );

    check(
        response.status == 500 &&
        !auth_called,
        "noncanonical trusted Director origin must fail closed"
    );
}


void test_origin_and_cookie_only_gate() {
    {
        auto context = good_context();
        auto request = good_request();

        request.headers.erase("Origin");

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 403,
            "missing Origin must be forbidden"
        );
    }

    {
        auto context = good_context();
        auto request = good_request();

        request.headers.emplace(
            "Origin",
            "https://director.example.test"
        );

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 403,
            "duplicate Origin must be forbidden"
        );
    }

    {
        auto context = good_context();
        auto request = good_request();

        request.headers.emplace(
            "Authorization",
            "Bearer attacker"
        );

        bool auth_called = false;

        context.require_user_cookie =
            [&](
                const httplib::Request&,
                httplib::Response&,
                std::string*,
                std::string*
            ) {
                auth_called = true;
                return true;
            };

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 403 &&
            !auth_called,
            "Bearer header must not enter cookie authentication"
        );
    }
}


void test_strict_body_validation() {
    {
        auto context = good_context();
        auto request = good_request();

        request.headers.erase(
            "Content-Type"
        );

        request.headers.emplace(
            "Content-Type",
            "text/plain"
        );

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 415,
            "non-JSON content type must be rejected"
        );
    }

    {
        auto context = good_context();
        auto request = good_request();

        request.body =
            std::string(
                "{\"browser_binding_hash\":\""
            ) +
            kBindingHash +
            "\",\"browser_binding_hash\":\"" +
            kBindingHash +
            "\"}";

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 400,
            "duplicate JSON key must be rejected"
        );
    }

    {
        auto context = good_context();
        auto request = good_request();

        request.body =
            json{
                {
                    "browser_binding_hash",
                    kBindingHash
                },
                {"home_node", "node-evil"}
            }.dump();

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 400,
            "browser must not prescribe home_node"
        );
    }

    {
        auto context = good_context();
        auto request = good_request();

        request.body =
            json{
                {
                    "browser_binding_hash",
                    "not-a-valid-binding"
                }
            }.dump();

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 400,
            "invalid binding hash must be rejected"
        );
    }
}


void test_rate_limit() {
    auto context = good_context();
    auto request = good_request();

    context.consume_rate_limit_slot =
        [](
            const std::string& fp,
            long* retry_after
        ) {
            check(
                fp == kFingerprint,
                "rate limiter key must be authenticated fingerprint"
            );

            *retry_after = 17;
            return false;
        };

    httplib::Response response;

    handle_director_storage_handoff_start_request(
        request,
        response,
        context
    );

    check(
        response.status == 429,
        "rate limit must return 429"
    );

    check(
        response.get_header_value(
            "Retry-After") == "17",
        "rate limit must return Retry-After"
    );
}


void test_authorization_state() {
    {
        auto context = good_context();
        auto request = good_request();

        context.resolve_authoritative_target =
            [](
                const std::string&
            ) {
                DirectorStorageHandoffTargetResult
                    result;

                result.code =
                    DirectorStorageHandoffTargetCode::
                        UserNotAuthorized;

                return result;
            };

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 403,
            "unauthorized user target must return 403"
        );
    }

    {
        auto context = good_context();
        auto request = good_request();

        context.resolve_authoritative_target =
            [](
                const std::string&
            ) {
                DirectorStorageHandoffTargetResult
                    result;

                result.code =
                    DirectorStorageHandoffTargetCode::
                        Unavailable;

                return result;
            };

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 503,
            "unavailable authoritative target must fail closed"
        );
    }
}


void test_issue_result_mapping() {
    {
        auto context = good_context();
        auto request = good_request();

        context.issue_ticket =
            [](
                const std::string&,
                const std::string&,
                const std::string&
            ) {
                StorageHandoffTicketClientResult
                    result;

                result.code =
                    StorageHandoffTicketClientCode::
                        RestartRequired;

                return result;
            };

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 409 &&
            response_json(response)["error"] ==
                "handoff_restart_required",
            "same-binding retry must require browser restart"
        );
    }

    {
        auto context = good_context();
        auto request = good_request();

        context.issue_ticket =
            [](
                const std::string&,
                const std::string&,
                const std::string&
            ) {
                StorageHandoffTicketClientResult
                    result;

                result.code =
                    StorageHandoffTicketClientCode::
                        RateLimited;

                return result;
            };

        httplib::Response response;

        handle_director_storage_handoff_start_request(
            request,
            response,
            context
        );

        check(
            response.status == 429,
            "Storage rate limit must remain rate limited"
        );
    }
}


void test_post_issue_authority_change_hides_ticket() {
    auto context = good_context();
    auto request = good_request();

    int resolve_calls = 0;

    context.resolve_authoritative_target =
        [&](
            const std::string&
        ) {
            ++resolve_calls;

            DirectorStorageHandoffTargetResult
                result;

            result.code =
                DirectorStorageHandoffTargetCode::Ok;

            result.target =
                good_target();

            if (resolve_calls == 2) {
                result.target.projection_revision = 8;
            }

            return result;
        };

    httplib::Response response;

    handle_director_storage_handoff_start_request(
        request,
        response,
        context
    );

    check(
        response.status == 409,
        "post-issue authority change must require restart"
    );

    check(
        response.body.find(kTicket) ==
            std::string::npos,
        "post-issue authority change must not reveal ticket"
    );

    check(
        resolve_calls == 2,
        "authority must be re-read after ticket issuance"
    );
}


void test_browser_destination_change_hides_ticket() {
    auto context = good_context();
    auto request = good_request();

    int resolve_calls = 0;

    context.resolve_authoritative_target =
        [&](
            const std::string&
        ) {
            ++resolve_calls;

            DirectorStorageHandoffTargetResult
                result;

            result.code =
                DirectorStorageHandoffTargetCode::Ok;

            result.target =
                good_target();

            if (resolve_calls == 2) {
                result.target.browser_base_url =
                    "https://storage-new.example.test";
            }

            return result;
        };

    httplib::Response response;

    handle_director_storage_handoff_start_request(
        request,
        response,
        context
    );

    check(
        response.status == 409 &&
        response.body.find(kTicket) ==
            std::string::npos,
        "changed browser destination must hide minted ticket"
    );
}


} // namespace


int main() {
    test_begin_returns_only_trusted_browser_origin();
    test_begin_security_gates();
    test_success_uses_authoritative_target();
    test_noncanonical_director_origin_fails_closed();
    test_origin_and_cookie_only_gate();
    test_strict_body_validation();
    test_rate_limit();
    test_authorization_state();
    test_issue_result_mapping();
    test_post_issue_authority_change_hides_ticket();
    test_browser_destination_change_hides_ticket();

    if (failures != 0) {
        std::cerr
            << failures
            << " Director storage handoff start test(s) failed\n";

        return EXIT_FAILURE;
    }

    std::cout
        << "OK: Director storage handoff start route tests passed\n";

    return EXIT_SUCCESS;
}
