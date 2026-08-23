#include "routes/routes_internal_storage_user_projection.h"

#include "cluster/internal_auth.h"
#include "cluster/storage_user_projection_store.h"
#include "httplib.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using json = nlohmann::json;
namespace fs = std::filesystem;

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

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-storage-projection-route-test-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* result =
        ::mkdtemp(
            writable.data()
        );

    if (!result) {
        return {};
    }

    return fs::path(result);
}

httplib::Request make_request(
    const std::string& authorization,
    const std::string& content_type,
    const std::string& body
) {
    httplib::Request request;

    request.remote_addr =
        "192.0.2.50";

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

json response_json(
    const httplib::Response& response
) {
    return json::parse(
        response.body,
        nullptr,
        false
    );
}

json projection_json(
    const std::string& fingerprint,
    std::uint64_t revision
) {
    return json{
        {"fingerprint", fingerprint},
        {"enabled", true},
        {"storage_allocated", true},
        {"quota_bytes", 1024},
        {
            "root_rel",
            "users/" + fingerprint
        },
        {"storage_pool_id", "pool_01"},
        {"home_node", "storage-01"},
        {"revision", revision}
    };
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium_init failed\n";

        return 1;
    }

    const fs::path test_directory =
        make_test_directory();

    if (test_directory.empty()) {
        std::cerr
            << "FAIL: could not create test directory\n";

        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;

        fs::remove_all(
            test_directory,
            error
        );
    };

    const fs::path store_path =
        test_directory /
        "storage_user_projections.json";

    pqnas::cluster::
        StorageUserProjectionStore store;

    std::string store_error;

    expect(
        store.load(
            store_path.string(),
            &store_error
        ),
        "projection store loads for route test: " +
            store_error
    );

    const std::string token =
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

    const std::string fingerprint(
        128,
        'a'
    );

    int rate_limit_calls = 0;
    bool allow_rate_limit = true;

    pqnas::cluster::
        InternalStorageUserProjectionRoutesContext
            context;

    context.verify_authorization =
        [&credential](
            const std::string& authorization
        ) {
            return pqnas::cluster::
                verify_internal_bearer_authorization(
                    authorization,
                    credential
                );
        };

    context.projection_store =
        &store;

    context.local_node_id =
        "storage-01";

    context.consume_rate_limit_slot =
        [&](const std::string& remote_address,
            long* retry_after_seconds) {
            ++rate_limit_calls;

            expect(
                remote_address ==
                    "192.0.2.50",
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

    {
        auto request =
            make_request(
                "",
                "application/json",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "missing authorization returns 401"
        );

        expect(
            rate_limit_calls == 0 &&
            store.snapshot().empty(),
            "missing authorization performs no projection work"
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
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "wrong authorization returns 401"
        );

        expect(
            rate_limit_calls == 0 &&
            store.snapshot().empty(),
            "bad credential does not consume rate limit or mutate store"
        );
    }

    // Security regression: verifier exceptions are indistinguishable from
    // failed bearer authentication and must perform no projection work.
    {
        const auto saved_verifier =
            context.verify_authorization;

        context.verify_authorization =
            [](const std::string&) -> bool {
                throw 1;
            };

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "throwing projection verifier must fail closed with 401"
        );

        expect(
            rate_limit_calls == 0 &&
            store.snapshot().empty(),
            "throwing projection verifier must not consume rate limit or mutate store"
        );

        expect(
            response_header(
                response,
                "WWW-Authenticate"
            ) == "Bearer",
            "throwing projection verifier returns generic bearer challenge"
        );

        context.verify_authorization =
            saved_verifier;
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        request.headers.emplace(
            "Authorization",
            "Bearer " + token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "duplicate Authorization headers are rejected"
        );

        expect(
            rate_limit_calls == 0 &&
            store.snapshot().empty(),
            "duplicate Authorization performs no projection work"
        );
    }

    {
        allow_rate_limit = false;

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
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
            store.snapshot().empty(),
            "rate limited request performs no mutation"
        );

        allow_rate_limit = true;
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "text/plain",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 415,
            "wrong content type returns 415"
        );

        expect(
            store.snapshot().empty(),
            "wrong content type performs no mutation"
        );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                std::string(
                    pqnas::cluster::
                        kInternalStorageUserProjectionMaxRequestBytes +
                        1,
                    'x'
                )
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 413,
            "oversized request returns 413"
        );

        expect(
            store.snapshot().empty(),
            "oversized request performs no mutation"
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
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "malformed JSON returns 400"
        );

        expect(
            store.snapshot().empty(),
            "malformed JSON performs no mutation"
        );
    }

    {
        json body =
            projection_json(
                fingerprint,
                1
            );

        body["extra"] = true;

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                body.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "unexpected JSON field returns 400"
        );

        expect(
            store.snapshot().empty(),
            "unexpected JSON field performs no mutation"
        );
    }

    {
        json body =
            projection_json(
                fingerprint,
                1
            );

        body["home_node"] =
            "storage-02";

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                body.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 409,
            "projection for another node returns 409"
        );

        expect(
            store.snapshot().empty(),
            "wrong-node projection is never installed"
        );
    }

    {
        json body =
            projection_json(
                fingerprint,
                1
            );

        body["root_rel"] =
            "users/foo/../bar";

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                body.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "non-canonical projection root returns 400"
        );

        expect(
            store.snapshot().empty(),
            "invalid projection performs no mutation"
        );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json; charset=utf-8",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200,
            "valid projection returns 200"
        );

        const json result =
            response_json(
                response
            );

        expect(
            result.is_object() &&
            result.value(
                "result",
                ""
            ) == "applied",
            "valid projection reports applied"
        );

        const auto stored =
            store.get(
                fingerprint
            );

        expect(
            stored.has_value() &&
            stored->revision == 1,
            "valid projection reaches store"
        );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        const json result =
            response_json(
                response
            );

        expect(
            response.status == 200 &&
            result.is_object() &&
            result.value(
                "result",
                ""
            ) == "unchanged",
            "identical same-revision retry is idempotent"
        );
    }

    {
        json body =
            projection_json(
                fingerprint,
                1
            );

        body["quota_bytes"] = 2048;

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                body.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 409 &&
            response.body.find(
                "projection_conflict"
            ) != std::string::npos,
            "same revision with different content conflicts"
        );
    }

    {
        json body =
            projection_json(
                fingerprint,
                2
            );

        body["quota_bytes"] = 2048;

        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                body.dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200 &&
            response.body.find(
                "\"applied\""
            ) != std::string::npos,
            "newer projection revision applies"
        );
    }

    {
        auto request =
            make_request(
                "Bearer " + token,
                "application/json",
                projection_json(
                    fingerprint,
                    1
                ).dump()
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_storage_user_projection_request(
                request,
                response,
                context
            );

        expect(
            response.status == 409 &&
            response.body.find(
                "stale_projection"
            ) != std::string::npos,
            "older projection revision is rejected as stale"
        );

        const auto stored =
            store.get(
                fingerprint
            );

        expect(
            stored.has_value() &&
            stored->revision == 2 &&
            stored->quota_bytes == 2048,
            "stale projection cannot roll back stored metadata"
        );
    }

    cleanup();

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " internal storage user projection route test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: internal storage user projection route tests passed\n";

    return 0;
}
