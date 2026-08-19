#include "routes/routes_storage_handoff_consume.h"

#include "cluster/storage_handoff_ticket_store.h"
#include "routes/routes_storage_handoff_preauth.h"
#include "httplib.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

httplib::Request make_request(
    const std::string& origin,
    const std::string& body,
    const std::string& cookie
) {
    httplib::Request request;

    request.remote_addr = "192.0.2.60";

    request.headers.emplace(
        "Origin",
        origin
    );

    request.headers.emplace(
        "Content-Type",
        "application/x-www-form-urlencoded"
    );

    request.headers.emplace(
        "Cookie",
        cookie
    );

    request.body = body;

    return request;
}

std::vector<std::string> set_cookies(
    const httplib::Response& response
) {
    std::vector<std::string> values;

    const auto range =
        response.headers.equal_range(
            "Set-Cookie"
        );

    for (auto iterator = range.first;
         iterator != range.second;
         ++iterator) {
        values.push_back(
            iterator->second
        );
    }

    return values;
}

bool has_prefix(
    const std::vector<std::string>& values,
    const std::string& prefix
) {
    for (const auto& value : values) {
        if (value.rfind(prefix, 0) == 0) {
            return true;
        }
    }

    return false;
}

} // namespace

int main() {
    const std::string director_origin =
        "https://director.example.test";

    const std::string ticket(
        pqnas::cluster::
            kStorageHandoffTicketChars,
        'A'
    );

    const std::string binding_secret(
        pqnas::cluster::
            kStorageHandoffBrowserBindingChars,
        'B'
    );

    const std::string body =
        "ticket=" + ticket;

    const std::string cookie =
        std::string(
            pqnas::cluster::
                kStorageHandoffPreauthCookieName
        ) +
        "=" +
        binding_secret;

    int rate_limit_calls = 0;
    int consume_calls = 0;
    bool allow_rate_limit = true;

    pqnas::cluster::
        StorageHandoffConsumeServiceResult
            service_result =
                pqnas::cluster::
                    StorageHandoffConsumeServiceResult::Ok;

    std::string service_cookie =
        "storage-session-token";

    pqnas::cluster::
        StorageHandoffConsumeRoutesContext context;

    context.expected_director_origin =
        director_origin;

    context.consume_rate_limit_slot =
        [&](const std::string& remote_address,
            long* retry_after_seconds) {
            ++rate_limit_calls;

            expect(
                remote_address == "192.0.2.60",
                "rate limiter receives transport peer"
            );

            if (!allow_rate_limit) {
                if (retry_after_seconds) {
                    *retry_after_seconds = 7;
                }

                return false;
            }

            if (retry_after_seconds) {
                *retry_after_seconds = 0;
            }

            return true;
        };

    context.consume =
        [&](const std::string& candidate_ticket,
            const std::string& candidate_binding,
            pqnas::cluster::
                StorageHandoffConsumeServiceOutput* output) {
            ++consume_calls;

            expect(
                candidate_ticket == ticket,
                "route passes exact ticket"
            );

            expect(
                candidate_binding == binding_secret,
                "route passes exact browser binding"
            );

            if (output &&
                service_result ==
                    pqnas::cluster::
                        StorageHandoffConsumeServiceResult::Ok) {
                output->session_cookie_value =
                    service_cookie;

                output->expires_at = 1600;
            }

            return service_result;
        };

    context.now_epoch =
        []() {
            return 1000L;
        };

    {
        auto request =
            make_request(
                director_origin,
                body,
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200,
            "valid handoff succeeds"
        );

        expect(
            rate_limit_calls == 1 &&
            consume_calls == 1,
            "valid handoff reaches limiter and service once"
        );

        expect(
            response.body.find(ticket) ==
                std::string::npos &&
            response.body.find(binding_secret) ==
                std::string::npos &&
            response.body.find(
                "storage-session-token"
            ) == std::string::npos,
            "response body exposes no bearer material"
        );

        const auto cookies =
            set_cookies(response);

        expect(
            cookies.size() == 2,
            "success emits exactly two Set-Cookie headers"
        );

        expect(
            has_prefix(
                cookies,
                "pqnas_storage_session="
                "storage-session-token;"
            ),
            "Storage session cookie is installed"
        );

        expect(
            has_prefix(
                cookies,
                "pqnas_storage_preauth=;"
            ),
            "one-time preauth cookie is cleared"
        );
    }

    {
        const int rate_before =
            rate_limit_calls;

        const int consume_before =
            consume_calls;

        auto request =
            make_request(
                "https://evil.example",
                body,
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "wrong Origin is rejected"
        );

        expect(
            rate_limit_calls == rate_before &&
            consume_calls == consume_before,
            "wrong Origin performs no security-state work"
        );
    }

    {
        const int consume_before =
            consume_calls;

        const std::string duplicate_cookie =
            cookie +
            "; " +
            cookie;

        auto request =
            make_request(
                director_origin,
                body,
                duplicate_cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "duplicate preauth cookies are rejected"
        );

        expect(
            consume_calls == consume_before,
            "cookie tossing does not consume ticket"
        );
    }

    {
        const int consume_before =
            consume_calls;

        auto request =
            make_request(
                director_origin,
                body,
                cookie
            );

        request.headers.emplace(
            "Origin",
            director_origin
        );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "duplicate Origin headers are rejected"
        );

        expect(
            consume_calls == consume_before,
            "duplicate Origin never reaches consume service"
        );
    }

    {
        const int consume_before =
            consume_calls;

        auto request =
            make_request(
                director_origin,
                body,
                cookie
            );

        request.headers.erase(
            "Content-Type"
        );

        request.headers.emplace(
            "Content-Type",
            "application/json"
        );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "wrong Content-Type is rejected"
        );

        expect(
            consume_calls == consume_before,
            "wrong Content-Type never reaches consume service"
        );
    }

    {
        service_result =
            pqnas::cluster::
                StorageHandoffConsumeServiceResult::
                    Rejected;

        auto request =
            make_request(
                director_origin,
                body,
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "service rejection is generic 403"
        );

        expect(
            response.body.find(
                "handoff_rejected"
            ) != std::string::npos,
            "service rejection exposes only generic error"
        );

        expect(
            set_cookies(response).empty(),
            "rejected handoff sets no cookies"
        );

        service_result =
            pqnas::cluster::
                StorageHandoffConsumeServiceResult::Ok;
    }

    {
        const std::string saved_cookie =
            service_cookie;

        service_cookie =
            "safe\r\nSet-Cookie: injected=1";

        auto request =
            make_request(
                director_origin,
                body,
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 503,
            "unsafe service cookie output fails closed"
        );

        expect(
            set_cookies(response).empty(),
            "unsafe cookie cannot inject response headers"
        );

        service_cookie =
            saved_cookie;
    }

    {
        allow_rate_limit = false;

        const int consume_before =
            consume_calls;

        auto request =
            make_request(
                director_origin,
                body,
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 429,
            "rate limited request returns 429"
        );

        const auto retry_after =
            response.headers.find(
                "Retry-After"
            );

        expect(
            retry_after != response.headers.end() &&
            retry_after->second == "7",
            "rate limited response includes Retry-After"
        );

        expect(
            consume_calls == consume_before,
            "rate limited request never consumes ticket"
        );

        expect(
            set_cookies(response).empty(),
            "rate limited request sets no cookies"
        );

        allow_rate_limit = true;
    }

    {
        const int consume_before =
            consume_calls;

        auto request =
            make_request(
                director_origin,
                body + "&extra=1",
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "extra form fields are rejected"
        );

        expect(
            consume_calls == consume_before,
            "malformed form never reaches consume service"
        );
    }

    {
        const int consume_before =
            consume_calls;

        auto request =
            make_request(
                director_origin,
                std::string(
                    pqnas::cluster::
                        kStorageHandoffConsumeMaxRequestBytes + 1,
                    'x'
                ),
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized consume body is rejected"
        );

        expect(
            consume_calls == consume_before,
            "oversized body never reaches consume service"
        );
    }

    {
        const long saved_now =
            1000L;

        context.now_epoch =
            [saved_now]() {
                return saved_now;
            };

        const int consume_before =
            consume_calls;

        const auto original_consume =
            context.consume;

        context.consume =
            [&](const std::string& candidate_ticket,
                const std::string& candidate_binding,
                pqnas::cluster::
                    StorageHandoffConsumeServiceOutput* output) {
                ++consume_calls;

                expect(
                    candidate_ticket == ticket,
                    "invalid-expiry case passes exact ticket"
                );

                expect(
                    candidate_binding == binding_secret,
                    "invalid-expiry case passes exact browser binding"
                );

                if (output) {
                    output->session_cookie_value =
                        "storage-session-token";

                    output->expires_at =
                        saved_now;
                }

                return pqnas::cluster::
                    StorageHandoffConsumeServiceResult::Ok;
            };

        auto request =
            make_request(
                director_origin,
                body,
                cookie
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_consume_request(
                request,
                response,
                context
            );

        expect(
            consume_calls == consume_before + 1,
            "invalid-expiry case reaches service exactly once"
        );

        expect(
            response.status == 503,
            "non-future session expiry fails closed"
        );

        expect(
            set_cookies(response).empty(),
            "invalid expiry sets no cookies"
        );

        context.consume =
            original_consume;
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " Storage handoff consume route test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage handoff consume route tests passed\n";

    return 0;
}
