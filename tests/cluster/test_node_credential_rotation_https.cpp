#include "cluster/node_credential_rotation_https.h"

#include "cluster/internal_auth.h"

#include <iostream>
#include <string>

namespace {

using pqnas::cluster::
    NodeCredentialRotationTransportCode;

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
    const std::string credential(
        43,
        'A'
    );

    expect(
        pqnas::cluster::
            valid_internal_auth_credential_token(
                credential
            ),
        "synthetic credential is canonical"
    );

    {
        auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    200,
                    "application/json; charset=utf-8",
                    std::string(
                        R"({"ok":true,"node_credential":")"
                    ) +
                        credential +
                        R"("})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    Ok &&
            response.http_status == 200 &&
            response.pending_credential ==
                credential,
            "strict prepare success response accepted"
        );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &response.pending_credential
            );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    200,
                    "application/json",
                    std::string(
                        R"({"ok":true,"node_credential":")"
                    ) +
                        credential +
                        R"(","extra":true})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
            "prepare success with extra fields rejected"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    200,
                    "application/json",
                    R"({"ok":true,"node_credential":"short"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    InvalidResponse &&
            response.pending_credential.empty(),
            "malformed prepare credential rejected"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    200,
                    "text/plain",
                    R"({"ok":true,"node_credential":"ignored"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
            "non-JSON prepare content type rejected"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    401,
                    "application/json",
                    R"({"ok":false,"error":"unauthorized"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    AuthenticationFailed &&
            response.http_status == 401,
            "prepare 401 maps to generic authentication failure"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    429,
                    "application/json",
                    R"({"ok":false,"error":"rate_limited"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    RateLimited,
            "prepare 429 rate limit mapped"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    503,
                    "application/json",
                    R"({"ok":false,"error":"rotation_unavailable"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    ServiceUnavailable,
            "prepare 503 unavailable mapped"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    503,
                    "application/json",
                    R"({"ok":false,"error":"unexpected_detail"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
            "unknown prepare error code rejected"
        );
    }

    {
        const std::string oversized(
            pqnas::cluster::
                kNodeCredentialRotationHttpsMaxResponseBytes +
                1,
            'x'
        );

        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_prepare_https_response(
                    200,
                    "application/json",
                    oversized
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
            "oversized prepare response rejected"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_finalize_https_response(
                    200,
                    "application/json; charset=utf-8",
                    R"({"ok":true})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    Ok &&
            response.http_status == 200,
            "strict finalize success accepted"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_finalize_https_response(
                    200,
                    "application/json",
                    std::string(
                        R"({"ok":true,"node_credential":")"
                    ) +
                        credential +
                        R"("})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
            "finalize success rejects unexpected credential field"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_finalize_https_response(
                    401,
                    "application/json",
                    R"({"ok":false,"error":"unauthorized"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    AuthenticationFailed,
            "finalize 401 authentication failure mapped"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_finalize_https_response(
                    429,
                    "application/json",
                    R"({"ok":false,"error":"rate_limited"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    RateLimited,
            "finalize rate limit mapped"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_finalize_https_response(
                    503,
                    "application/json",
                    R"({"ok":false,"error":"rotation_unavailable"})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    ServiceUnavailable,
            "finalize unavailable mapped"
        );
    }

    {
        const auto response =
            pqnas::cluster::
                parse_node_credential_rotation_finalize_https_response(
                    200,
                    "application/json",
                    R"({"ok":false})"
                );

        expect(
            response.code ==
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
            "false finalize success rejected"
        );
    }

    if (failures == 0) {
        std::cout
            << "OK: node credential rotation HTTPS parser tests passed\n";

        return 0;
    }

    std::cerr
        << "FAIL: "
        << failures
        << " node credential rotation HTTPS parser test(s) failed\n";

    return 1;
}
