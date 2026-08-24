#include "cluster/internal_auth.h"
#include "cluster/storage_user_projection_audit_https.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <utility>

namespace {

int failures = 0;

using Code =
    pqnas::cluster::
        StorageUserProjectionAuditTransportCode;

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

pqnas::cluster::StorageUserProjectionAuditRequest
request_fixture() {
    pqnas::cluster::StorageUserProjectionAuditRequest
        request;

    request.node_id = "storage-01";

    pqnas::cluster::StorageUserProjectionAuditExpected
        expected;

    expected.fingerprint =
        std::string(128, 'a');
    expected.revision = 17;
    expected.digest_hex =
        std::string(
            pqnas::cluster::
                kStorageUserProjectionDigestHexChars,
            'b'
        );

    request.expected.push_back(
        std::move(expected)
    );

    return request;
}

void expect_code(
    int status,
    const std::string& body,
    Code expected_code,
    const std::string& message
) {
    const auto result =
        pqnas::cluster::
            parse_storage_user_projection_audit_https_response(
                status,
                "application/json; charset=utf-8",
                body,
                request_fixture()
            );

    expect(
        result.code ==
            expected_code,
        message
    );
}

} // namespace

int main() {
    {
        pqnas::cluster::
            StorageUserProjectionAuditTransportRequest
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

        request.audit =
            request_fixture();

        const auto transport =
            pqnas::cluster::
                make_https_storage_user_projection_audit_transport();

        const auto result =
            transport(request);

        expect(
            result.code ==
                Code::InvalidResponse &&
            result.http_status == 0,
            "malformed TLS pin fails before network transport"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":1,"safe_tombstones_ignored":0,"mismatches":[]})",
                    request_fixture()
                );

        expect(
            result.code == Code::Ok &&
            result.http_status == 200 &&
            result.result.complete &&
            result.result.matched == 1 &&
            result.result.mismatches.empty(),
            "complete matching audit proof accepted"
        );
    }

    {
        const auto fingerprint =
            std::string(128, 'a');

        const std::string body =
            std::string(
                R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":0,"safe_tombstones_ignored":0,"mismatches":[{"fingerprint":")"
            ) +
            fingerprint +
            R"(","kind":"conflict","expected_revision":17,"actual_revision":17}]})";

        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    body,
                    request_fixture()
                );

        expect(
            result.code == Code::Ok &&
            result.result.complete &&
            result.result.matched == 0 &&
            result.result.mismatches.size() == 1 &&
            result.result.mismatches[0].code ==
                pqnas::cluster::
                    StorageUserProjectionAuditMismatchCode::
                        Conflict,
            "same-revision conflict proof accepted"
        );
    }

    {
        const auto fingerprint =
            std::string(128, 'c');

        const std::string body =
            std::string(
                R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":1,"safe_tombstones_ignored":0,"mismatches":[{"fingerprint":")"
            ) +
            fingerprint +
            R"(","kind":"unexpected_authorization","expected_revision":0,"actual_revision":9}]})";

        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    body,
                    request_fixture()
                );

        expect(
            result.code == Code::Ok &&
            result.result.mismatches.size() == 1 &&
            result.result.mismatches[0].code ==
                pqnas::cluster::
                    StorageUserProjectionAuditMismatchCode::
                        UnexpectedAuthorization,
            "unexpected authorization proof accepted"
        );
    }

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
        409,
        R"({"ok":false,"error":"wrong_node"})",
        Code::WrongNode,
        "wrong-node audit classified"
    );

    expect_code(
        429,
        R"({"ok":false,"error":"rate_limited"})",
        Code::RateLimited,
        "rate limit classified"
    );

    expect_code(
        503,
        R"({"ok":false,"error":"audit_unavailable"})",
        Code::ServiceUnavailable,
        "audit service failure classified"
    );

    {
        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    503,
                    "application/json",
                    R"({"ok":false,"error":"audit_unavailable"})",
                    request_fixture()
                );

        expect(
            result.code ==
                Code::ServiceUnavailable &&
            !result.result.complete,
            "failed HTTPS audit response can never be complete"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    R"({"version":1,"ok":true,"node_id":"storage-02","complete":true,"matched":1,"safe_tombstones_ignored":0,"mismatches":[]})",
                    request_fixture()
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "success for wrong node rejected"
        );
    }

    {
        auto request =
            request_fixture();

        request.range_start =
            std::string(128, 'a');
        request.range_end =
            std::string(128, 'b');

        const auto outside_fingerprint =
            std::string(128, 'c');

        const std::string body =
            std::string(
                R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":1,"safe_tombstones_ignored":0,"mismatches":[{"fingerprint":")"
            ) +
            outside_fingerprint +
            R"(","kind":"unexpected_authorization","expected_revision":0,"actual_revision":9}]})";

        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    body,
                    request
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "out-of-range unexpected authorization is rejected"
        );
    }

    {
        const auto fingerprint =
            std::string(128, 'a');

        const std::string body =
            std::string(
                R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":0,"safe_tombstones_ignored":0,"mismatches":[{"fingerprint":")"
            ) +
            fingerprint +
            R"(","kind":"unknown_kind","expected_revision":17,"actual_revision":17}]})";

        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    body,
                    request_fixture()
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "unknown mismatch kind rejected"
        );
    }

    {
        const auto fingerprint =
            std::string(128, 'a');

        const std::string body =
            std::string(
                R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":0,"safe_tombstones_ignored":0,"mismatches":[{"fingerprint":")"
            ) +
            fingerprint +
            R"(","kind":"stale","expected_revision":17,"actual_revision":18}]})";

        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    body,
                    request_fixture()
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "stale mismatch with non-stale revision rejected"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":0,"safe_tombstones_ignored":0,"mismatches":[]})",
                    request_fixture()
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "complete response must account for every expected projection"
        );
    }

    {
        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":1,"safe_tombstones_ignored":0,"mismatches":[],"extra":1})",
                    request_fixture()
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
                parse_storage_user_projection_audit_https_response(
                    200,
                    "text/plain",
                    R"({"version":1,"ok":true,"node_id":"storage-01","complete":true,"matched":1,"safe_tombstones_ignored":0,"mismatches":[]})",
                    request_fixture()
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
                kStorageUserProjectionAuditHttpsMaxResponseBytes +
                1,
            'x'
        );

        const auto result =
            pqnas::cluster::
                parse_storage_user_projection_audit_https_response(
                    200,
                    "application/json",
                    oversized,
                    request_fixture()
                );

        expect(
            result.code ==
                Code::InvalidResponse,
            "oversized audit response rejected"
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " projection audit HTTPS test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: storage user projection audit HTTPS tests passed\n";
    return 0;
}
