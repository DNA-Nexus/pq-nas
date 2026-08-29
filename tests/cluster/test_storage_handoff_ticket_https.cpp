#include "cluster/internal_auth.h"
#include "cluster/storage_handoff_ticket_https.h"
#include "cluster/storage_handoff_ticket_store.h"

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

using Code =
    pqnas::cluster::
        StorageHandoffTicketTransportCode;

std::string valid_ticket() {
    return std::string(
        pqnas::cluster::
            kStorageHandoffTicketChars,
        'A'
    );
}

void expect_code(
    int status,
    const std::string& body,
    Code expected_code,
    const std::string& message
) {
    const auto result =
        pqnas::cluster::
            parse_storage_handoff_ticket_https_response(
                status,
                "application/json; charset=utf-8",
                body
            );

    expect(
        result.code == expected_code,
        message
    );
}

} // namespace

int main() {
    {
        pqnas::cluster::
            StorageHandoffTicketTransportRequest
                request;

        request.api_base_url =
            "https://storage.example:8443";

        request.authorization =
            "Bearer " +
            std::string(
                pqnas::cluster::
                    kInternalAuthTokenChars,
                'A'
            );

        request.tls_spki_sha256_pin =
            "sha256/not-base64";

        request.fingerprint =
            std::string(128, 'a');

        request.browser_binding_hash =
            std::string(
                pqnas::cluster::
                    kStorageHandoffBrowserBindingChars,
                'A'
            );

        const auto transport =
            pqnas::cluster::
                make_https_storage_handoff_ticket_transport();

        const auto result =
            transport(request);

        expect(
            result.code ==
                Code::InvalidResponse &&
            result.http_status == 0,
            "malformed TLS pin fails before network transport"
        );
    }

    const std::string ticket =
        valid_ticket();

    {
        const auto result =
            pqnas::cluster::
                parse_storage_handoff_ticket_https_response(
                    200,
                    "application/json",
                    std::string(
                        R"({"ok":true,"ticket":")"
                    ) +
                    ticket +
                    R"(","expires_at":123456})"
                );

        expect(
            result.code == Code::Ok &&
            result.http_status == 200 &&
            result.ticket == ticket &&
            result.expires_at == 123456,
            "strict success response accepted"
        );
    }

    {
        std::string noncanonical =
            ticket;

        noncanonical.back() = 'B';

        const auto result =
            pqnas::cluster::
                parse_storage_handoff_ticket_https_response(
                    200,
                    "application/json",
                    std::string(
                        R"({"ok":true,"ticket":")"
                    ) +
                    noncanonical +
                    R"(","expires_at":123456})"
                );

        expect(
            result.code ==
                Code::InvalidResponse &&
            result.ticket.empty(),
            "non-canonical ticket rejected"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_handoff_ticket_https_response(
                    200,
                    "application/json",
                    std::string(
                        R"({"ok":true,"ticket":")"
                    ) +
                    ticket +
                    R"(","ticket":")" +
                    ticket +
                    R"(","expires_at":123456})"
                );

        expect(
            result.code ==
                Code::InvalidResponse &&
            result.ticket.empty(),
            "duplicate success fields rejected"
        );
    }

    expect_code(
        400,
        R"({"ok":false,"error":"bad_request","error":"bad_request"})",
        Code::InvalidResponse,
        "duplicate error fields rejected"
    );

    expect_code(
        400,
        R"({"ok":false,"error":"bad_request"})",
        Code::Rejected,
        "bad request classified"
    );

    expect_code(
        401,
        R"({"ok":false,"error":"unauthorized"})",
        Code::AuthenticationFailed,
        "authentication failure classified"
    );

    expect_code(
        403,
        R"({"ok":false,"error":"user_not_authorized"})",
        Code::UserNotAuthorized,
        "user authorization failure classified"
    );

    expect_code(
        409,
        R"({"ok":false,"error":"handoff_restart_required"})",
        Code::RestartRequired,
        "handoff restart classified"
    );

    expect_code(
        409,
        R"({"ok":false,"error":"rate_limited"})",
        Code::InvalidResponse,
        "409 with wrong error code rejected"
    );

    expect_code(
        429,
        R"({"ok":false,"error":"rate_limited"})",
        Code::RateLimited,
        "rate limit classified"
    );

    expect_code(
        503,
        R"({"ok":false,"error":"handoff_unavailable"})",
        Code::ServiceUnavailable,
        "handoff service failure classified"
    );

    expect_code(
        415,
        R"({"ok":false,"error":"unsupported_media_type"})",
        Code::InvalidResponse,
        "unexpected protocol status rejected"
    );

    expect_code(
        500,
        R"({"ok":false,"error":"server_error"})",
        Code::InvalidResponse,
        "unknown server response rejected"
    );

    {
        const auto result =
            pqnas::cluster::
                parse_storage_handoff_ticket_https_response(
                    200,
                    "application/json",
                    std::string(
                        R"({"ok":true,"ticket":")"
                    ) +
                    ticket +
                    R"(","expires_at":123456,"extra":1})"
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "extra success fields rejected"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_handoff_ticket_https_response(
                    200,
                    "application/json",
                    std::string(
                        R"({"ok":true,"ticket":")"
                    ) +
                    ticket +
                    R"(","expires_at":0})"
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "non-positive expiry rejected"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_handoff_ticket_https_response(
                    200,
                    "text/plain",
                    R"({"ok":true})"
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "non-JSON content type rejected"
        );
    }

    {
        const std::string oversized(
            pqnas::cluster::
                kStorageHandoffTicketMaxResponseBytes +
                1,
            'x'
        );

        const auto result =
            pqnas::cluster::
                parse_storage_handoff_ticket_https_response(
                    200,
                    "application/json",
                    oversized
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "oversized response rejected"
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " handoff ticket HTTPS test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage handoff ticket HTTPS tests passed\n";

    return 0;
}
