#include "cluster/storage_node_enrollment_https.h"

#include "cluster/internal_auth.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <iostream>
#include <string>

namespace {

using json = nlohmann::json;

using pqnas::cluster::
    StorageNodeEnrollmentTransportCode;
using pqnas::cluster::
    StorageNodeEnrollmentTransportRequest;

int failures = 0;

constexpr char kDirectorTlsPin[] =
    "sha256/"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

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

void expect_code(
    int status,
    const std::string& content_type,
    const std::string& body,
    StorageNodeEnrollmentTransportCode expected,
    const std::string& message
) {
    const auto result =
        pqnas::cluster::
            parse_storage_node_enrollment_https_response(
                status,
                content_type,
                body
            );

    expect(
        result.code == expected,
        message
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
        const auto result =
            pqnas::cluster::
                parse_storage_node_enrollment_https_response(
                    200,
                    "application/json; charset=utf-8",
                    R"({"ok":true,"recovered":false})"
                );

        expect(
            result.code ==
                StorageNodeEnrollmentTransportCode::
                    Ok &&
            !result.recovered &&
            result.http_status == 200,
            "fresh success response contract"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_node_enrollment_https_response(
                    200,
                    "APPLICATION/JSON",
                    R"({"ok":true,"recovered":true})"
                );

        expect(
            result.code ==
                StorageNodeEnrollmentTransportCode::
                    Ok &&
            result.recovered,
            "recovered success response contract"
        );
    }

    expect_code(
        200,
        "application/json",
        R"({"ok":true,"recovered":false,"extra":true})",
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        "success response rejects unknown fields"
    );

    expect_code(
        200,
        "application/json",
        R"({"ok":false,"recovered":false})",
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        "success response requires ok true"
    );

    expect_code(
        401,
        "application/json",
        R"({"ok":false,"error":"enrollment_auth_failed"})",
        StorageNodeEnrollmentTransportCode::
            AuthenticationFailed,
        "generic authentication response"
    );

    expect_code(
        401,
        "application/json",
        R"({"ok":false,"error":"different_error"})",
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        "401 rejects unexpected disclosure contract"
    );

    expect_code(
        409,
        "application/json",
        R"({"ok":false,"error":"node_enrollment_conflict"})",
        StorageNodeEnrollmentTransportCode::
            Conflict,
        "conflict response"
    );

    expect_code(
        429,
        "application/json",
        R"({"ok":false,"error":"rate_limited"})",
        StorageNodeEnrollmentTransportCode::
            RateLimited,
        "rate limit response"
    );

    expect_code(
        503,
        "application/json",
        R"({"ok":false,"error":"enrollment_consumption_service_unavailable"})",
        StorageNodeEnrollmentTransportCode::
            ServiceUnavailable,
        "service unavailable response"
    );

    expect_code(
        200,
        "text/plain",
        R"({"ok":true,"recovered":false})",
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        "non-JSON content type rejected"
    );

    expect_code(
        200,
        "application/json",
        "{",
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        "malformed JSON rejected"
    );

    expect_code(
        500,
        "application/json",
        R"({"ok":false,"error":"server_error"})",
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        "unexpected HTTP status rejected"
    );

    expect_code(
        200,
        "application/json",
        std::string(
            pqnas::cluster::
                kStorageNodeEnrollmentMaxResponseBytes +
            1,
            'X'
        ),
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        "oversized response rejected"
    );

    std::string token;
    std::string error;

    expect(
        pqnas::cluster::
            generate_internal_auth_credential_token(
                &token,
                &error
            ),
        "generate HTTPS validation token: " +
            error
    );

    StorageNodeEnrollmentTransportRequest request;

    request.director_url =
        "https://director.internal";

    request.director_tls_spki_sha256_pin =
        kDirectorTlsPin;

    request.authorization_token =
        token;

    request.enrollment_id =
        "enrollment_https_validation";

    request.expected_generation =
        1;

    request.node_id =
        "storage-https-validation";

    request.api_base_url =
        "https://storage-https-validation.internal";

    request.protocol_version =
        7;

    request.tls_spki_sha256_pin =
        "sha256/"
        "lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=";

    request.node_credential =
        token;

    const auto transport =
        pqnas::cluster::
            make_https_storage_node_enrollment_transport();

    const std::string valid_director_pin =
        request.director_tls_spki_sha256_pin;

    request.director_tls_spki_sha256_pin.clear();

    const auto missing_director_pin_result =
        transport(request);

    expect(
        missing_director_pin_result.code ==
            StorageNodeEnrollmentTransportCode::
                InvalidResponse &&
        missing_director_pin_result.http_status == 0,
        "production transport rejects missing Director TLS pin before network"
    );

    request.director_tls_spki_sha256_pin =
        " " + valid_director_pin;

    const auto noncanonical_director_pin_result =
        transport(request);

    expect(
        noncanonical_director_pin_result.code ==
            StorageNodeEnrollmentTransportCode::
                InvalidResponse &&
        noncanonical_director_pin_result.http_status == 0,
        "production transport rejects noncanonical Director TLS pin before network"
    );

    request.director_tls_spki_sha256_pin =
        valid_director_pin;

    request.director_url =
        "http://director.internal";

    const auto invalid_url_result =
        transport(request);

    expect(
        invalid_url_result.code ==
            StorageNodeEnrollmentTransportCode::
                InvalidResponse &&
        invalid_url_result.http_status == 0,
        "production transport rejects plaintext Director URL before network"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &request.authorization_token
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &request.node_credential
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &token
        );

    if (failures != 0) {
        std::cerr
            << failures
            << " storage node enrollment HTTPS test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage node enrollment HTTPS tests passed\n";

    return 0;
}
