#include "routes/routes_storage_handoff_preauth.h"

#include "cluster/storage_handoff_ticket_store.h"
#include "httplib.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

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

httplib::Request make_request(
    const std::string& origin,
    const std::string& body = {}
) {
    httplib::Request request;

    request.remote_addr =
        "192.0.2.55";

    if (!origin.empty()) {
        request.headers.emplace(
            "Origin",
            origin
        );
    }

    request.body = body;

    return request;
}

std::string response_header(
    const httplib::Response& response,
    const std::string& name
) {
    const auto found =
        response.headers.find(name);

    if (found ==
        response.headers.end()) {
        return {};
    }

    return found->second;
}

std::string binding_secret_from_cookie(
    const std::string& cookie
) {
    const std::string prefix =
        std::string(
            pqnas::cluster::
                kStorageHandoffPreauthCookieName
        ) +
        "=";

    if (cookie.rfind(prefix, 0) != 0) {
        return {};
    }

    const std::size_t end =
        cookie.find(
            ';',
            prefix.size()
        );

    if (end == std::string::npos) {
        return {};
    }

    return cookie.substr(
        prefix.size(),
        end - prefix.size()
    );
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium_init failed\n";

        return 1;
    }

    const std::string expected_origin =
        "https://storage-01.example.test";

    int rate_limit_calls = 0;
    bool allow_request = true;
    bool throw_from_limiter = false;

    pqnas::cluster::
        StorageHandoffPreauthRoutesContext context;

    context.expected_origin =
        expected_origin;

    context.consume_rate_limit_slot =
        [&](const std::string& remote_address,
            long* retry_after_seconds) {
            ++rate_limit_calls;

            expect(
                remote_address ==
                    "192.0.2.55",
                "rate limiter receives transport peer"
            );

            if (throw_from_limiter) {
                throw std::runtime_error(
                    "synthetic limiter failure"
                );
            }

            if (!allow_request) {
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

    {
        auto request =
            make_request(
                expected_origin
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200,
            "valid same-origin preauth succeeds"
        );

        expect(
            rate_limit_calls == 1,
            "valid request consumes one rate-limit slot"
        );

        expect(
            response_header(
                response,
                "Cache-Control"
            ) == "no-store",
            "success is not cacheable"
        );

        expect(
            response_header(
                response,
                "X-Content-Type-Options"
            ) == "nosniff",
            "success has nosniff"
        );

        json body;

        try {
            body = json::parse(
                response.body
            );
        } catch (...) {
            body = json{};
        }

        expect(
            body.is_object() &&
            body.size() == 2 &&
            body.value(
                "ok",
                false
            ) &&
            body.contains(
                "browser_binding_hash"
            ) &&
            body[
                "browser_binding_hash"
            ].is_string(),
            "success schema contains only ok and binding hash"
        );

        const std::string binding_hash =
            body.value(
                "browser_binding_hash",
                std::string{}
            );

        expect(
            pqnas::cluster::
                valid_storage_handoff_browser_binding_hash(
                    binding_hash
                ),
            "response contains canonical binding hash"
        );

        const std::string cookie =
            response_header(
                response,
                "Set-Cookie"
            );

        const std::string binding_secret =
            binding_secret_from_cookie(
                cookie
            );

        expect(
            binding_secret.size() ==
                pqnas::cluster::
                    kStorageHandoffBrowserBindingChars,
            "cookie contains canonical browser secret"
        );

        std::string derived_hash;

        expect(
            pqnas::cluster::
                storage_handoff_browser_binding_hash(
                    binding_secret,
                    &derived_hash
                ) &&
            derived_hash ==
                binding_hash,
            "cookie B hashes to response H"
        );

        expect(
            cookie.find(
                "; Path=/api/storage/handoff/consume"
            ) != std::string::npos &&
            cookie.find(
                "; Max-Age=120"
            ) != std::string::npos &&
            cookie.find(
                "; HttpOnly"
            ) != std::string::npos &&
            cookie.find(
                "; SameSite=Strict"
            ) != std::string::npos &&
            cookie.find(
                "; Secure"
            ) != std::string::npos,
            "preauth cookie has hardened narrow attributes"
        );

        expect(
            response.body.find(
                binding_secret
            ) == std::string::npos,
            "plaintext B is never returned in JSON"
        );

        expect(
            cookie.find(
                binding_hash
            ) == std::string::npos,
            "cookie contains B rather than Director-visible H"
        );
    }

    {
        auto request =
            make_request(
                ""
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "missing Origin is rejected"
        );

        expect(
            rate_limit_calls == 1,
            "missing Origin does not consume rate-limit slot"
        );

        expect(
            response_header(
                response,
                "Set-Cookie"
            ).empty(),
            "missing Origin mints no browser secret"
        );
    }

    {
        auto request =
            make_request(
                "https://evil.example"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "cross-origin request is rejected"
        );

        expect(
            rate_limit_calls == 1,
            "cross-origin request cannot burn victim rate limit"
        );
    }

    {
        auto request =
            make_request(
                expected_origin
            );

        request.headers.emplace(
            "Origin",
            expected_origin
        );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "duplicate Origin headers are rejected"
        );

        expect(
            rate_limit_calls == 1,
            "ambiguous Origin consumes no rate-limit slot"
        );
    }

    {
        auto request =
            make_request(
                expected_origin,
                "{}"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "preauth rejects request body"
        );

        expect(
            rate_limit_calls == 1,
            "invalid body consumes no rate-limit slot"
        );
    }

    {
        auto request =
            make_request(
                expected_origin,
                std::string(
                    pqnas::cluster::
                        kStorageHandoffPreauthMaxRequestBytes +
                        1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized preauth body is rejected"
        );

        expect(
            rate_limit_calls == 1,
            "oversized body consumes no rate-limit slot"
        );
    }

    {
        allow_request = false;

        auto request =
            make_request(
                expected_origin
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 429,
            "rate limited request returns 429"
        );

        expect(
            response_header(
                response,
                "Retry-After"
            ) == "7",
            "rate limited response includes Retry-After"
        );

        expect(
            response_header(
                response,
                "Set-Cookie"
            ).empty(),
            "rate limited request mints no secret"
        );

        allow_request = true;
    }

    {
        throw_from_limiter = true;

        auto request =
            make_request(
                expected_origin
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                context
            );

        expect(
            response.status == 503,
            "limiter exception fails closed"
        );

        expect(
            response_header(
                response,
                "Set-Cookie"
            ).empty(),
            "limiter failure mints no secret"
        );

        throw_from_limiter = false;
    }

    {
        auto bad_context =
            context;

        bad_context.expected_origin =
            "http://storage-01.example.test";

        const int calls_before =
            rate_limit_calls;

        auto request =
            make_request(
                expected_origin
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                bad_context
            );

        expect(
            response.status == 500,
            "non-HTTPS configured origin fails closed"
        );

        expect(
            rate_limit_calls ==
                calls_before,
            "invalid trusted origin performs no request work"
        );

        expect(
            response_header(
                response,
                "Set-Cookie"
            ).empty(),
            "invalid configuration mints no secret"
        );
    }

    {
        auto bad_context =
            context;

        bad_context.expected_origin =
            expected_origin + "/";

        auto request =
            make_request(
                expected_origin
            );

        httplib::Response response;

        pqnas::cluster::
            handle_storage_handoff_preauth_request(
                request,
                response,
                bad_context
            );

        expect(
            response.status == 500,
            "non-canonical configured origin fails closed"
        );
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " storage handoff preauth test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage handoff preauth route tests passed\n";

    return 0;
}
