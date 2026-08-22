#include "routes/routes_internal_node_status.h"

#include "cluster/internal_auth.h"
#include "httplib.h"

#include <sodium.h>

#include <array>
#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

using json = nlohmann::json;

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

bool write_all(int fd, const std::string& value) {
    std::size_t offset = 0;

    while (offset < value.size()) {
        const ssize_t count = ::write(
            fd,
            value.data() + offset,
            value.size() - offset
        );

        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        return false;
    }

    return true;
}

std::string synthetic_token() {
    std::array<
        unsigned char,
        pqnas::cluster::kInternalAuthSecretBytes
    > bytes{};

    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<unsigned char>(index + 1);
    }

    std::array<
        char,
        sodium_base64_ENCODED_LEN(
            pqnas::cluster::kInternalAuthSecretBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        )
    > encoded{};

    if (!sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            bytes.data(),
            bytes.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING)) {
        return {};
    }

    sodium_memzero(bytes.data(), bytes.size());
    return std::string(encoded.data());
}

bool load_test_credential(
    const std::string& token,
    pqnas::cluster::InternalAuthCredential* credential
) {
    char path[] =
        "/tmp/pqnas-node-route-credential-XXXXXX";

    const int fd = ::mkstemp(path);

    if (fd < 0) {
        return false;
    }

    bool ok =
        ::fchmod(fd, 0600) == 0 &&
        write_all(fd, token + "\n");

    if (::close(fd) != 0) {
        ok = false;
    }

    std::string error;

    if (ok) {
        ok =
            pqnas::cluster::
                load_internal_auth_credential_file(
                    path,
                    credential,
                    &error
                );
    }

    ::unlink(path);
    return ok;
}

std::string response_header(
    const httplib::Response& response,
    const std::string& name
) {
    const auto found = response.headers.find(name);

    if (found == response.headers.end()) {
        return {};
    }

    return found->second;
}

httplib::Request make_request(
    const std::string& remote_addr,
    const std::string& authorization = ""
) {
    httplib::Request request;
    request.remote_addr = remote_addr;

    if (!authorization.empty()) {
        request.headers.emplace(
            "Authorization",
            authorization
        );
    }

    return request;
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr << "FAIL: sodium_init failed\n";
        return 1;
    }

    const std::string token = synthetic_token();

    expect(
        token.size() ==
            pqnas::cluster::kInternalAuthTokenChars,
        "synthetic token length"
    );

    pqnas::cluster::InternalAuthCredential credential;

    expect(
        load_test_credential(token, &credential),
        "load synthetic credential"
    );

    int builder_calls = 0;
    std::int64_t fake_now = 1000;

    pqnas::cluster::InternalNodeStatusRoutesContext context;
    context.credential = &credential;
    context.now_epoch_sec = [&fake_now]() {
        return fake_now;
    };

    context.build_status_json =
        [&builder_calls](
            json* out,
            std::string* error
        ) {
            ++builder_calls;

            if (error) {
                error->clear();
            }

            if (!out) {
                return false;
            }

            *out = json{
                {"ok", true},
                {"protocol_version", 1},
                {"node_id", "node-01"},
                {"role", "node"}
            };

            return true;
        };

    {
        auto request =
            make_request("192.0.2.10");

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_status_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "missing authorization should return 401"
        );

        expect(
            builder_calls == 0,
            "missing authorization must not collect status"
        );

        expect(
            response_header(response, "Cache-Control") ==
                "no-store",
            "unauthorized response should disable caching"
        );
    }

    {
        std::string wrong_token = token;
        wrong_token[0] =
            wrong_token[0] == 'A' ? 'B' : 'A';

        auto request = make_request(
            "192.0.2.11",
            "Bearer " + wrong_token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_status_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "wrong authorization should return 401"
        );

        expect(
            builder_calls == 0,
            "wrong authorization must not collect status"
        );

        expect(
            response.body.find(wrong_token) ==
                std::string::npos,
            "response must not reflect credentials"
        );
    }

    {
        auto request = make_request(
            "192.0.2.12",
            "Bearer " + token
        );

        request.headers.emplace(
            "Authorization",
            "Bearer " + token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_status_request(
                request,
                response,
                context
            );

        expect(
            response.status == 401,
            "duplicated authorization must be rejected"
        );

        expect(
            builder_calls == 0,
            "duplicated authorization must not collect status"
        );
    }

    {
        auto request = make_request(
            "192.0.2.13",
            "Bearer " + token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_status_request(
                request,
                response,
                context
            );

        expect(
            response.status == 200,
            "valid authorization should return 200"
        );

        expect(
            builder_calls == 1,
            "valid authorization should collect status once"
        );

        expect(
            response_header(response, "Cache-Control") ==
                "no-store",
            "successful response should disable caching"
        );

        try {
            const json body = json::parse(response.body);

            expect(
                body["node_id"] == "node-01",
                "successful response node ID"
            );

            expect(
                body["role"] == "node",
                "successful response role"
            );
        } catch (...) {
            expect(false, "successful response should contain JSON");
        }
    }

    {
        const int calls_before_unauth_flood =
            builder_calls;

        // Security: unauthenticated traffic must not consume the authenticated
        // service-to-service status budget. Otherwise a reachable peer could
        // starve legitimate Director status polling from the same transport IP.
        for (int index = 0;
             index <
                 pqnas::cluster::
                     kInternalNodeStatusRateLimit + 5;
             ++index) {
            auto request =
                make_request("192.0.2.19");

            httplib::Response response;

            pqnas::cluster::
                handle_internal_node_status_request(
                    request,
                    response,
                    context
                );

            expect(
                response.status == 401,
                "unauthenticated status flood must remain 401"
            );
        }

        expect(
            builder_calls ==
                calls_before_unauth_flood,
            "unauthenticated status flood must not collect status"
        );

        auto authenticated_request =
            make_request(
                "192.0.2.19",
                "Bearer " + token
            );

        httplib::Response authenticated_response;

        pqnas::cluster::
            handle_internal_node_status_request(
                authenticated_request,
                authenticated_response,
                context
            );

        expect(
            authenticated_response.status == 200,
            "unauthenticated traffic must not exhaust authenticated status rate limit"
        );

        expect(
            builder_calls ==
                calls_before_unauth_flood + 1,
            "authenticated request after unauthenticated flood must collect status"
        );
    }

    {
        const int calls_before_rate_test =
            builder_calls;

        for (int index = 0;
             index <
                 pqnas::cluster::
                     kInternalNodeStatusRateLimit;
             ++index) {
            auto request = make_request(
                "192.0.2.20",
                "Bearer " + token
            );

            httplib::Response response;

            pqnas::cluster::
                handle_internal_node_status_request(
                    request,
                    response,
                    context
                );

            expect(
                response.status == 200,
                "requests within rate limit should succeed"
            );
        }

        auto request = make_request(
            "192.0.2.20",
            "Bearer " + token
        );

        httplib::Response response;

        pqnas::cluster::
            handle_internal_node_status_request(
                request,
                response,
                context
            );

        expect(
            response.status == 429,
            "request above rate limit should return 429"
        );

        expect(
            response_header(response, "Retry-After") ==
                "60",
            "rate-limited response should include Retry-After"
        );

        expect(
            builder_calls ==
                calls_before_rate_test +
                pqnas::cluster::
                    kInternalNodeStatusRateLimit,
            "rate-limited request must not collect status"
        );
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " internal node route test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: internal node status route tests passed\n";
    return 0;
}
