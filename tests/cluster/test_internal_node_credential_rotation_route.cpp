#include "routes/routes_internal_node_credential_rotation.h"

#include "cluster/internal_auth.h"
#include "cluster/storage_node_credential_runtime.h"
#include "httplib.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

using json = nlohmann::json;

using pqnas::cluster::
    InternalNodeCredentialRotationRoutesContext;

using pqnas::cluster::
    StorageNodeCredentialRuntime;

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

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-node-credential-route-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back(
        '\0'
    );

    char* created =
        ::mkdtemp(
            writable.data()
        );

    return created
        ? fs::path(created)
        : fs::path{};
}

std::string synthetic_token(
    unsigned char seed
) {
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
                seed +
                static_cast<unsigned char>(
                    index
                )
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

    return
        std::string(
            encoded.data()
        );
}

bool write_private_text(
    const fs::path& path,
    const std::string& text
) {
    const int descriptor =
        ::open(
            path.c_str(),
            O_WRONLY |
            O_CREAT |
            O_TRUNC |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

    if (descriptor < 0) {
        return false;
    }

    const std::string contents =
        text +
        "\n";

    std::size_t offset = 0;

    while (offset <
           contents.size()) {
        const ssize_t count =
            ::write(
                descriptor,
                contents.data() + offset,
                contents.size() - offset
            );

        if (count > 0) {
            offset +=
                static_cast<std::size_t>(
                    count
                );

            continue;
        }

        if (count < 0 &&
            errno == EINTR) {
            continue;
        }

        (void)::close(
            descriptor
        );

        return false;
    }

    const bool file_synced =
        ::fsync(
            descriptor
        ) == 0;

    const bool closed =
        ::close(
            descriptor
        ) == 0;

    const int directory_descriptor =
        ::open(
            path.parent_path().c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    bool directory_synced = false;

    if (directory_descriptor >= 0) {
        directory_synced =
            ::fsync(
                directory_descriptor
            ) == 0;

        (void)::close(
            directory_descriptor
        );
    }

    return
        file_synced &&
        closed &&
        directory_synced;
}

httplib::Request request_with_auth(
    const std::string& remote_address,
    const std::string& token
) {
    httplib::Request request;

    request.remote_addr =
        remote_address;

    if (!token.empty()) {
        request.headers.emplace(
            "Authorization",
            "Bearer " + token
        );
    }

    return request;
}

std::string header(
    const httplib::Response& response,
    const std::string& name
) {
    const auto found =
        response.headers.find(
            name
        );

    if (found ==
        response.headers.end()) {
        return {};
    }

    return
        found->second;
}

bool response_ok(
    const httplib::Response& response
) {
    const json body =
        json::parse(
            response.body,
            nullptr,
            false
        );

    return
        body.is_object() &&
        body.value(
            "ok",
            false
        );
}

std::string response_credential(
    const httplib::Response& response
) {
    json body =
        json::parse(
            response.body,
            nullptr,
            false
        );

    if (!body.is_object()) {
        return {};
    }

    const auto found =
        body.find(
            "node_credential"
        );

    if (found ==
            body.end() ||
        !found->is_string()) {
        return {};
    }

    return
        found->get<std::string>();
}

void cleanup(
    const fs::path& root
) {
    std::error_code error;

    fs::remove_all(
        root,
        error
    );
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";

        return 1;
    }

    const fs::path root =
        make_test_directory();

    if (root.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    const fs::path active =
        root /
        "cluster_node.credential";

    const std::string old_token =
        synthetic_token(1);

    const std::string wrong_token =
        synthetic_token(91);

    expect(
        write_private_text(
            active,
            old_token
        ),
        "write route active credential fixture"
    );

    StorageNodeCredentialRuntime
        runtime;

    std::string load_error;

    expect(
        runtime.load(
            active.string(),
            &load_error
        ),
        "load credential runtime for route tests: " +
            load_error
    );

    InternalNodeCredentialRotationRoutesContext
        context;

    context.runtime =
        &runtime;

    // Missing Authorization fails closed and does not create pending state.
    {
        auto request =
            request_with_auth(
                "192.0.2.10",
                ""
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "prepare without authorization returns 401"
        );

        expect(
            !runtime.rotation_pending(),
            "unauthorized prepare creates no pending credential"
        );

        expect(
            header(
                response,
                "WWW-Authenticate"
            ) == "Bearer",
            "unauthorized prepare advertises bearer authentication"
        );
    }

    // Wrong bearer fails closed.
    {
        auto request =
            request_with_auth(
                "192.0.2.11",
                wrong_token
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "wrong prepare bearer returns 401"
        );

        expect(
            response.body.find(
                wrong_token
            ) == std::string::npos,
            "wrong bearer is not reflected"
        );

        expect(
            !runtime.rotation_pending(),
            "wrong bearer creates no pending credential"
        );
    }

    // Duplicate Authorization is rejected rather than ambiguously parsed.
    {
        auto request =
            request_with_auth(
                "192.0.2.12",
                old_token
            );

        request.headers.emplace(
            "Authorization",
            "Bearer " + old_token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "duplicate prepare Authorization returns 401"
        );

        expect(
            !runtime.rotation_pending(),
            "duplicate Authorization creates no pending credential"
        );
    }

    // Rotation mutation endpoints do not accept request bodies.
    {
        auto request =
            request_with_auth(
                "192.0.2.13",
                old_token
            );

        request.body =
            "{}";

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                request,
                response,
                context
            );

        expect(
            response.status == 400,
            "prepare with a body returns 400"
        );

        expect(
            !runtime.rotation_pending(),
            "bad prepare body creates no pending credential"
        );
    }

    std::string new_token;

    // OLD may prepare NEW.
    {
        auto request =
            request_with_auth(
                "192.0.2.20",
                old_token
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200 &&
            response_ok(
                response
            ),
            "OLD prepares NEW successfully"
        );

        new_token =
            response_credential(
                response
            );

        expect(
            new_token.size() ==
                pqnas::cluster::
                    kInternalAuthTokenChars &&
            new_token != old_token,
            "prepare response contains distinct NEW credential"
        );

        expect(
            runtime.rotation_pending(),
            "prepare establishes pending rotation"
        );

        expect(
            runtime.verify_authorization(
                "Bearer " +
                old_token
            ),
            "OLD remains valid during overlap"
        );

        expect(
            runtime.verify_authorization(
                "Bearer " +
                new_token
            ),
            "NEW is valid during overlap"
        );

        expect(
            header(
                response,
                "Cache-Control"
            ) == "no-store",
            "prepare credential response disables caching"
        );
    }

    // Prepare retry through NEW returns the exact existing pending credential.
    {
        auto request =
            request_with_auth(
                "192.0.2.21",
                new_token
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                request,
                response,
                context
            );

        std::string retry_token =
            response_credential(
                response
            );

        expect(
            response.status == 200 &&
            retry_token == new_token,
            "prepare retry returns the same pending credential"
        );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &retry_token
            );
    }

    // Unauthenticated traffic must not consume the authenticated mutation
    // budget. Otherwise a reachable peer could starve legitimate Director
    // rotation attempts before possessing any valid credential.
    {
        for (std::size_t index = 0;
             index <
                 pqnas::cluster::
                     kInternalNodeCredentialRotationRateLimitRequests + 5;
             ++index) {
            auto request =
                request_with_auth(
                    "192.0.2.40",
                    wrong_token
                );

            httplib::Response response;

            pqnas::cluster::
                handle_internal_node_credential_rotation_prepare_request(
                    request,
                    response,
                    context
                );

            expect(
                response.status == 401,
                "unauthenticated prepare flood remains 401"
            );
        }

        auto legitimate_request =
            request_with_auth(
                "192.0.2.40",
                new_token
            );

        httplib::Response legitimate_response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                legitimate_request,
                legitimate_response,
                context
            );

        std::string retry_token =
            response_credential(
                legitimate_response
            );

        expect(
            legitimate_response.status == 200 &&
            retry_token == new_token,
            "unauthenticated flood does not exhaust Director rotation budget"
        );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &retry_token
            );
    }

    // Authenticated mutation traffic is bounded per transport peer.
    {
        for (std::size_t index = 0;
             index <
                 pqnas::cluster::
                     kInternalNodeCredentialRotationRateLimitRequests;
             ++index) {
            auto request =
                request_with_auth(
                    "192.0.2.41",
                    new_token
                );

            httplib::Response response;

            pqnas::cluster::
                handle_internal_node_credential_rotation_prepare_request(
                    request,
                    response,
                    context
                );

            expect(
                response.status == 200,
                "authenticated prepare within rate limit succeeds"
            );
        }

        auto limited_request =
            request_with_auth(
                "192.0.2.41",
                new_token
            );

        httplib::Response limited_response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                limited_request,
                limited_response,
                context
            );

        expect(
            limited_response.status == 429,
            "authenticated prepare exceeding rate limit returns 429"
        );

        expect(
            !header(
                limited_response,
                "Retry-After"
            ).empty(),
            "rate-limited rotation response contains Retry-After"
        );

        // A busy or malicious authenticated peer must not globally exhaust
        // rotation for another transport peer.
        auto independent_request =
            request_with_auth(
                "192.0.2.42",
                new_token
            );

        httplib::Response independent_response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_prepare_request(
                independent_request,
                independent_response,
                context
            );

        expect(
            independent_response.status == 200,
            "rotation rate limit remains per-peer"
        );
    }

    // OLD is valid for ordinary overlap authentication but may never retire
    // itself by finalizing the credential generation.
    {
        auto request =
            request_with_auth(
                "192.0.2.30",
                old_token
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_finalize_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "OLD cannot finalize pending rotation"
        );

        expect(
            runtime.rotation_pending(),
            "failed OLD finalize keeps pending state"
        );

        expect(
            runtime.verify_authorization(
                "Bearer " +
                old_token
            ) &&
            runtime.verify_authorization(
                "Bearer " +
                new_token
            ),
            "failed OLD finalize preserves overlap"
        );
    }

    // NEW proves that Director already possesses the pending generation and
    // may retire OLD.
    {
        auto request =
            request_with_auth(
                "192.0.2.31",
                new_token
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_finalize_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200 &&
            response_ok(
                response
            ),
            "NEW finalizes rotation"
        );

        expect(
            !runtime.rotation_pending(),
            "successful finalize clears pending state"
        );

        expect(
            !runtime.verify_authorization(
                "Bearer " +
                old_token
            ),
            "OLD rejected after finalize"
        );

        expect(
            runtime.verify_authorization(
                "Bearer " +
                new_token
            ),
            "NEW remains active after finalize"
        );
    }

    // Lost successful finalize responses are retry-safe with current ACTIVE.
    {
        auto request =
            request_with_auth(
                "192.0.2.32",
                new_token
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_finalize_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200 &&
            response_ok(
                response
            ),
            "finalize retry with ACTIVE is idempotent"
        );
    }

    // Retired OLD no longer authenticates even for a finalize retry.
    {
        auto request =
            request_with_auth(
                "192.0.2.33",
                old_token
            );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_credential_rotation_finalize_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "retired OLD cannot retry finalize"
        );
    }

    pqnas::cluster::
        clear_internal_auth_token_text(
            &new_token
        );

    cleanup(
        root
    );

    if (failures == 0) {
        std::cout
            << "OK: internal node credential rotation route tests passed\n";

        return 0;
    }

    std::cerr
        << "FAIL: "
        << failures
        << " internal node credential rotation route test(s) failed\n";

    return 1;
}
