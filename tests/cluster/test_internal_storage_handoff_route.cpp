#include "routes/routes_internal_storage_handoff.h"

#include "cluster/internal_auth.h"
#include "cluster/storage_handoff_ticket_store.h"
#include "httplib.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <array>
#include <cstdint>
#include <iostream>
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

std::string synthetic_token() {
    std::array<
        unsigned char,
        pqnas::cluster::
            kInternalAuthSecretBytes
    > bytes{};

    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<unsigned char>(
                index + 1
            );
    }

    std::array<
        char,
        sodium_base64_ENCODED_LEN(
            pqnas::cluster::
                kInternalAuthSecretBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        )
    > encoded{};

    if (!sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            bytes.data(),
            bytes.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING)) {
        sodium_memzero(
            bytes.data(),
            bytes.size()
        );

        return {};
    }

    sodium_memzero(
        bytes.data(),
        bytes.size()
    );

    return std::string(
        encoded.data()
    );
}

httplib::Request make_request(
    const std::string& authorization,
    const std::string& content_type,
    const std::string& body
) {
    httplib::Request request;
    request.remote_addr =
        "192.0.2.40";

    if (!authorization.empty()) {
        request.headers.emplace(
            "Authorization",
            authorization
        );
    }

    if (!content_type.empty()) {
        request.headers.emplace(
            "Content-Type",
            content_type
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

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium_init failed\n";

        return 1;
    }

    std::string token =
        synthetic_token();

    expect(
        token.size() ==
            pqnas::cluster::
                kInternalAuthTokenChars,
        "synthetic internal auth token"
    );

    pqnas::cluster::
        InternalAuthCredential credential;

    std::string credential_error;

    expect(
        pqnas::cluster::
            parse_internal_auth_credential_token(
                token,
                &credential,
                &credential_error
            ),
        "parse internal auth credential"
    );

    std::int64_t now = 1000;

    pqnas::cluster::
        StorageHandoffTicketStore store(
            16,
            [&now]() {
                return now;
            }
        );

    const std::string fingerprint(
        pqnas::cluster::
            kStorageHandoffFingerprintHexChars,
        'a'
    );

    const std::string home_node =
        "storage-01";

    constexpr std::uint64_t
        projection_revision = 17;

    int authorize_calls = 0;
    bool allow_user = true;

    int rate_limit_calls = 0;
    bool allow_rate_limited_request = true;

    pqnas::cluster::
        InternalStorageHandoffRoutesContext context;

    context.credential =
        &credential;

    context.ticket_store =
        &store;

    context.consume_rate_limit_slot =
        [&](const std::string& remote_address,
            long* retry_after_seconds) {
            ++rate_limit_calls;

            expect(
                remote_address == "192.0.2.40",
                "rate limiter receives transport peer"
            );

            if (!allow_rate_limited_request) {
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

    context.authorize_user =
        [&](const std::string& candidate,
            std::string* out_home_node,
            std::uint64_t* out_projection_revision) {
            ++authorize_calls;

            if (!allow_user ||
                candidate != fingerprint) {
                return false;
            }

            if (out_home_node) {
                *out_home_node = home_node;
            }

            if (out_projection_revision) {
                *out_projection_revision =
                    projection_revision;
            }

            return true;
        };

    {
        auto request =
            make_request(
                "",
                "application/json",
                json{
                    {"fingerprint", fingerprint}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "missing authorization returns 401"
        );

        expect(
            authorize_calls == 0 &&
            store.size() == 0,
            "missing authorization performs no handoff work"
        );

        expect(
            response_header(
                response,
                "WWW-Authenticate"
            ) == "Bearer",
            "unauthorized response advertises bearer auth"
        );
    }

    {
        std::string wrong_token =
            token;

        wrong_token[0] =
            wrong_token[0] == 'A'
                ? 'B'
                : 'A';

        auto request =
            make_request(
                "Bearer " + wrong_token,
                "application/json",
                json{
                    {"fingerprint", fingerprint}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "wrong authorization returns 401"
        );

        expect(
            authorize_calls == 0 &&
            store.size() == 0,
            "wrong authorization performs no handoff work"
        );

        expect(
            response.body.find(
                wrong_token
            ) == std::string::npos,
            "authorization credential is never reflected"
        );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &wrong_token
            );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                json{
                    {"fingerprint", fingerprint}
                }.dump()
            );

        request.headers.emplace(
            "Authorization",
            "Bearer " + token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "duplicated authorization returns 401"
        );

        expect(
            authorize_calls == 0 &&
            store.size() == 0,
            "duplicated authorization performs no handoff work"
        );
    }

    {
        expect(
            rate_limit_calls == 0,
            "failed bearer authentication does not consume rate limit"
        );

        allow_rate_limited_request = false;

        auto request =
            make_request(
                "Bearer " + token,
                "text/plain",
                "{}"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
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
            "rate limited response advertises retry delay"
        );

        expect(
            rate_limit_calls == 1 &&
            authorize_calls == 0 &&
            store.size() == 0,
            "rate limiter stops request before handoff work"
        );

        expect(
            response.body.find(
                "rate_limited"
            ) != std::string::npos,
            "rate limited response has generic error"
        );

        allow_rate_limited_request = true;
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "text/plain",
                json{
                    {"fingerprint", fingerprint}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 415,
            "wrong content type returns 415"
        );

        expect(
            authorize_calls == 0 &&
            store.size() == 0,
            "wrong content type performs no handoff work"
        );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                "{"
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "malformed JSON returns 400"
        );

        expect(
            authorize_calls == 0 &&
            store.size() == 0,
            "malformed JSON performs no handoff work"
        );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                json{
                    {"fingerprint", fingerprint},
                    {"extra", true}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "unexpected JSON field returns 400"
        );

        expect(
            authorize_calls == 0 &&
            store.size() == 0,
            "unexpected JSON field performs no handoff work"
        );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                json{
                    {"fingerprint",
                     std::string(127, 'a')}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "invalid fingerprint returns 400"
        );

        expect(
            authorize_calls == 0 &&
            store.size() == 0,
            "invalid fingerprint performs no handoff work"
        );
    }

    {
        allow_user = false;

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                json{
                    {"fingerprint", fingerprint}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 403,
            "unauthorized user returns 403"
        );

        expect(
            authorize_calls == 1 &&
            store.size() == 0,
            "unauthorized user receives no ticket"
        );

        allow_user = true;
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json; charset=utf-8",
                json{
                    {"fingerprint", fingerprint}
                }.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_handoff_ticket_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200,
            "authorized request returns 200"
        );

        expect(
            authorize_calls == 2,
            "authorized request checks user once"
        );

        expect(
            response_header(
                response,
                "Cache-Control"
            ) == "no-store",
            "ticket response disables caching"
        );

        expect(
            response_header(
                response,
                "X-Content-Type-Options"
            ) == "nosniff",
            "ticket response disables content sniffing"
        );

        try {
            const json body =
                json::parse(
                    response.body
                );

            expect(
                body.value("ok", false),
                "success body ok"
            );

            expect(
                body.value(
                    "expires_at",
                    static_cast<std::int64_t>(0)
                ) == 1030,
                "success body expiry"
            );

            std::string handoff_ticket =
                body.value(
                    "ticket",
                    std::string{}
                );

            expect(
                handoff_ticket.size() ==
                    pqnas::cluster::
                        kStorageHandoffTicketChars,
                "success body ticket format"
            );

            pqnas::cluster::
                StorageHandoffTicketClaims claims;

            expect(
                store.consume(
                    handoff_ticket,
                    &claims
                ) ==
                    pqnas::cluster::
                        StorageHandoffTicketResult::Ok,
                "returned ticket exists in Storage store"
            );

            expect(
                claims.fingerprint ==
                    fingerprint &&
                claims.home_node ==
                    home_node &&
                claims.projection_revision ==
                    projection_revision,
                "returned ticket is bound to projection snapshot"
            );

            pqnas::cluster::
                clear_storage_handoff_ticket_text(
                    &handoff_ticket
                );
        } catch (...) {
            expect(
                false,
                "success response contains valid JSON"
            );
        }
    }

    pqnas::cluster::
        clear_internal_auth_token_text(
            &token
        );

    if (failures != 0) {
        std::cerr
            << failures
            << " internal storage handoff route test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: internal storage handoff route tests passed\n";

    return 0;
}
