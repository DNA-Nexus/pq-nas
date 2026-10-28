#include "cluster/storage_user_projection_sync_https.h"
#include "cluster/internal_auth.h"

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
        StorageUserProjectionSyncTransportCode;

void expect_code(
    int status,
    const std::string& body,
    std::uint64_t expected_revision,
    Code expected_code,
    const std::string& message
) {
    const auto result =
        pqnas::cluster::
            parse_storage_user_projection_sync_https_response(
                status,
                "application/json; charset=utf-8",
                body,
                expected_revision
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
            StorageUserProjectionSyncTransportRequest
                request;

        request.api_base_url =
            "https://storage.example:8443";

        // Valid bearer syntax, but deliberately malformed TLS identity.
        request.authorization =
            "Bearer " +
            std::string(
                pqnas::cluster::
                    kInternalAuthTokenChars,
                'A'
            );

        request.tls_spki_sha256_pin =
            "sha256/not-base64";

        request.projection.fingerprint =
            std::string(128, 'a');
        request.projection.enabled = true;
        request.projection.storage_allocated = false;
        request.projection.home_node = "storage-01";
        request.projection.revision = 17;

        const auto transport =
            pqnas::cluster::
                make_https_storage_user_projection_sync_transport();

        const auto result =
            transport(request);

        expect(
            result.code ==
                Code::InvalidResponse &&
            result.http_status == 0,
            "malformed TLS pin fails before network transport"
        );
    }

    expect_code(
        200,
        R"({"ok":true,"result":"applied","revision":17})",
        17,
        Code::Applied,
        "applied response accepted"
    );

    expect_code(
        200,
        R"({"ok":true,"result":"unchanged","revision":17})",
        17,
        Code::Unchanged,
        "unchanged response accepted"
    );

    expect_code(
        200,
        R"({"ok":true,"result":"applied","revision":16})",
        17,
        Code::InvalidResponse,
        "success for wrong revision rejected"
    );

    expect_code(
        400,
        R"({"ok":false,"error":"invalid_projection"})",
        17,
        Code::Rejected,
        "invalid projection response classified"
    );

    expect_code(
        401,
        R"({"ok":false,"error":"unauthorized"})",
        17,
        Code::AuthenticationFailed,
        "generic authentication failure classified"
    );

    expect_code(
        409,
        R"({"ok":false,"error":"wrong_node"})",
        17,
        Code::WrongNode,
        "wrong-node conflict classified"
    );

    expect_code(
        409,
        R"({"ok":false,"error":"stale_projection"})",
        17,
        Code::Stale,
        "stale projection classified"
    );

    expect_code(
        409,
        R"({"ok":false,"error":"projection_conflict"})",
        17,
        Code::Conflict,
        "same-revision conflict classified"
    );

    expect_code(
        429,
        R"({"ok":false,"error":"rate_limited"})",
        17,
        Code::RateLimited,
        "rate limit classified"
    );

    expect_code(
        503,
        R"({"ok":false,"error":"projection_unavailable"})",
        17,
        Code::ServiceUnavailable,
        "projection service failure classified"
    );

    expect_code(
        409,
        R"({"ok":false,"error":"something_else"})",
        17,
        Code::InvalidResponse,
        "unknown conflict code rejected"
    );

    expect_code(
        200,
        R"({"ok":true,"result":"applied","revision":17,"extra":1})",
        17,
        Code::InvalidResponse,
        "extra success fields rejected"
    );

    {
        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_sync_https_response(
                    200,
                    "text/plain",
                    R"({"ok":true,"result":"applied","revision":17})",
                    17
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
                kStorageUserProjectionSyncMaxResponseBytes +
                1,
            'x'
        );

        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_sync_https_response(
                    200,
                    "application/json",
                    oversized,
                    17
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
            << " projection sync HTTPS test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage user projection sync HTTPS tests passed\n";

    return 0;
}
