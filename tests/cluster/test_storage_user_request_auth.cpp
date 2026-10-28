#include "cluster/storage_user_request_auth.h"

#include "httplib.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

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

} // namespace

int main() {
    using pqnas::cluster::
        StorageUserRequestAuthContext;

    using pqnas::cluster::
        StorageUserSessionAuthenticatorOutput;

    using pqnas::cluster::
        StorageUserSessionAuthenticatorResult;

    const std::string storage_cookie =
        "storage-session-token.v1";

    const std::string storage_cookie_header =
        std::string(
            pqnas::cluster::
                kStorageUserRequestSessionCookieName
        ) +
        "=" +
        storage_cookie;

    int authenticate_calls = 0;

    std::string last_cookie;

    StorageUserSessionAuthenticatorResult
        authenticate_result =
            StorageUserSessionAuthenticatorResult::
                Authenticated;

    StorageUserRequestAuthContext context;

    context.authenticate =
        [&](const std::string& cookie_value,
            StorageUserSessionAuthenticatorOutput* output) {
            ++authenticate_calls;

            last_cookie =
                cookie_value;

            if (authenticate_result ==
                    StorageUserSessionAuthenticatorResult::
                        Authenticated &&
                output) {
                output->fingerprint =
                    std::string(
                        128,
                        'a'
                    );

                output->role =
                    "user";

                output->projection_revision =
                    17;

                output->expires_at =
                    1600;
            }

            return authenticate_result;
        };


    // ------------------------------------------------------------------
    // Valid Storage session.
    // ------------------------------------------------------------------
    {
        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint;
        std::string role;
        std::uint64_t projection_revision = 0;

        const int calls_before =
            authenticate_calls;

        expect(
            pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role,
                    &projection_revision
                ),
            "valid Storage session authenticates"
        );

        expect(
            authenticate_calls ==
                calls_before + 1,
            "valid request calls authenticator exactly once"
        );

        expect(
            last_cookie ==
                storage_cookie,
            "authenticator receives exact Storage cookie value"
        );

        expect(
            fingerprint ==
                std::string(128, 'a') &&
            role == "user" &&
            projection_revision == 17,
            "successful request returns revision-bound Storage identity"
        );
    }


    // ------------------------------------------------------------------
    // Missing Cookie header.
    // ------------------------------------------------------------------
    {
        httplib::Request request;
        httplib::Response response;

        std::string fingerprint =
            "stale";

        std::string role =
            "admin";

        std::uint64_t projection_revision =
            999;

        const int calls_before =
            authenticate_calls;

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role,
                    &projection_revision
                ),
            "missing cookie is rejected"
        );

        expect(
            response.status == 401,
            "missing cookie returns 401"
        );

        expect(
            authenticate_calls ==
                calls_before,
            "missing cookie never reaches authenticator"
        );

        expect(
            fingerprint.empty() &&
            role.empty() &&
            projection_revision == 0,
            "missing cookie clears revision-bound identity outputs"
        );
    }


    // ------------------------------------------------------------------
    // Director browser session must not work on Storage.
    // ------------------------------------------------------------------
    {
        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            "pqnas_session=director-session-token"
        );

        httplib::Response response;

        std::string fingerprint;
        std::string role;

        const int calls_before =
            authenticate_calls;

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role
                ),
            "Director pqnas_session is rejected"
        );

        expect(
            response.status == 401,
            "Director cookie returns 401"
        );

        expect(
            authenticate_calls ==
                calls_before,
            "Director cookie never reaches Storage authenticator"
        );
    }


    // ------------------------------------------------------------------
    // No mixed Bearer + Storage-cookie authentication.
    // ------------------------------------------------------------------
    {
        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        request.headers.emplace(
            "Authorization",
            "Bearer director-mobile-token"
        );

        httplib::Response response;

        std::string fingerprint;
        std::string role;

        const int calls_before =
            authenticate_calls;

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role
                ),
            "Authorization header is rejected on Storage user boundary"
        );

        expect(
            response.status == 401,
            "Bearer presence returns 401"
        );

        expect(
            authenticate_calls ==
                calls_before,
            "Bearer request never reaches Storage authenticator"
        );
    }


    // ------------------------------------------------------------------
    // Duplicate Cookie headers.
    // ------------------------------------------------------------------
    {
        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint;
        std::string role;

        const int calls_before =
            authenticate_calls;

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role
                ),
            "duplicate Cookie headers are rejected"
        );

        expect(
            response.status == 401,
            "duplicate Cookie headers return 401"
        );

        expect(
            authenticate_calls ==
                calls_before,
            "duplicate Cookie headers never reach authenticator"
        );
    }


    // ------------------------------------------------------------------
    // Cookie tossing: duplicate Storage cookie name in one header.
    // ------------------------------------------------------------------
    {
        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header +
                "; other=value; " +
                storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint;
        std::string role;

        const int calls_before =
            authenticate_calls;

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role
                ),
            "duplicate Storage session cookie is rejected"
        );

        expect(
            response.status == 401,
            "cookie tossing returns 401"
        );

        expect(
            authenticate_calls ==
                calls_before,
            "cookie tossing never reaches authenticator"
        );
    }


    // ------------------------------------------------------------------
    // Dynamic authentication rejection stays generic.
    // ------------------------------------------------------------------
    {
        authenticate_result =
            StorageUserSessionAuthenticatorResult::
                Rejected;

        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint =
            "stale";

        std::string role =
            "admin";

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role
                ),
            "authenticator rejection fails request"
        );

        expect(
            response.status == 401,
            "authenticator rejection returns generic 401"
        );

        expect(
            response.body.find(
                "unauthorized"
            ) != std::string::npos,
            "rejected session exposes only generic unauthorized error"
        );

        expect(
            response.body.find(
                storage_cookie
            ) == std::string::npos,
            "rejected response never reflects bearer cookie"
        );

        expect(
            fingerprint.empty() &&
            role.empty(),
            "rejected authentication leaves no identity"
        );

        authenticate_result =
            StorageUserSessionAuthenticatorResult::
                Authenticated;
    }


    // ------------------------------------------------------------------
    // Authenticator infrastructure failure.
    // ------------------------------------------------------------------
    {
        authenticate_result =
            StorageUserSessionAuthenticatorResult::
                InternalError;

        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint;
        std::string role;

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    context,
                    &fingerprint,
                    &role
                ),
            "authenticator internal failure fails closed"
        );

        expect(
            response.status == 503,
            "authenticator internal failure returns 503"
        );

        expect(
            response.body.find(
                storage_cookie
            ) == std::string::npos,
            "infrastructure failure never reflects bearer cookie"
        );

        authenticate_result =
            StorageUserSessionAuthenticatorResult::
                Authenticated;
    }


    // ------------------------------------------------------------------
    // Missing authenticator configuration.
    // ------------------------------------------------------------------
    {
        StorageUserRequestAuthContext
            invalid_context;

        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint =
            "stale";

        std::string role =
            "admin";

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    invalid_context,
                    &fingerprint,
                    &role
                ),
            "missing authenticator fails closed"
        );

        expect(
            response.status == 500,
            "missing authenticator returns 500"
        );

        expect(
            fingerprint.empty() &&
            role.empty(),
            "configuration failure clears identity outputs"
        );
    }


    // ------------------------------------------------------------------
    // Authenticator callback exception.
    // ------------------------------------------------------------------
    {
        StorageUserRequestAuthContext
            throwing_context;

        throwing_context.authenticate =
            [](const std::string&,
               StorageUserSessionAuthenticatorOutput*)
                -> StorageUserSessionAuthenticatorResult {
                throw 1;
            };

        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint;
        std::string role;

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    throwing_context,
                    &fingerprint,
                    &role
                ),
            "authenticator exception fails closed"
        );

        expect(
            response.status == 503,
            "authenticator exception returns 503"
        );
    }


    // ------------------------------------------------------------------
    // Authenticator contract violation.
    // ------------------------------------------------------------------
    {
        StorageUserRequestAuthContext
            broken_context;

        broken_context.authenticate =
            [](const std::string&,
               StorageUserSessionAuthenticatorOutput*)
                -> StorageUserSessionAuthenticatorResult {
                return
                    StorageUserSessionAuthenticatorResult::
                        Authenticated;
            };

        httplib::Request request;

        request.headers.emplace(
            "Cookie",
            storage_cookie_header
        );

        httplib::Response response;

        std::string fingerprint =
            "stale";

        std::string role =
            "admin";

        expect(
            !pqnas::cluster::
                require_storage_user_request_auth(
                    request,
                    response,
                    broken_context,
                    &fingerprint,
                    &role
                ),
            "invalid authenticated output fails closed"
        );

        expect(
            response.status == 503,
            "invalid authenticated output returns 503"
        );

        expect(
            fingerprint.empty() &&
            role.empty(),
            "invalid authenticated output exposes no identity"
        );
    }


    if (failures != 0) {
        std::cerr
            << failures
            << " Storage user request auth test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage user request auth tests passed\n";

    return 0;
}
