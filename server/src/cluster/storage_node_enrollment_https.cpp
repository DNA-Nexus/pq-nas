#include "cluster/storage_node_enrollment_https.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_tls_https_client.h"
#include "cluster/node_tls_identity.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kStorageNodeEnrollmentMaxRequestBytes =
        4 * 1024;

constexpr std::size_t
    kMaximumEnrollmentIdBytes =
        128;

struct HttpsAuthority {
    std::string host;
    int port = 443;
};

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_internal_auth_token_text(
            &value_
        );
    }

    SensitiveStringWiper(
        const SensitiveStringWiper&
    ) = delete;

    SensitiveStringWiper& operator=(
        const SensitiveStringWiper&
    ) = delete;

private:
    std::string& value_;
};

class HeaderCredentialWiper {
public:
    explicit HeaderCredentialWiper(
        httplib::Headers& headers
    ) noexcept
        : headers_(headers) {}

    ~HeaderCredentialWiper() {
        const auto range =
            headers_.equal_range(
                "Authorization"
            );

        for (auto iterator = range.first;
             iterator != range.second;
             ++iterator) {
            clear_internal_auth_token_text(
                &iterator->second
            );
        }
    }

    HeaderCredentialWiper(
        const HeaderCredentialWiper&
    ) = delete;

    HeaderCredentialWiper& operator=(
        const HeaderCredentialWiper&
    ) = delete;

private:
    httplib::Headers& headers_;
};

class JsonCredentialWiper {
public:
    explicit JsonCredentialWiper(
        json& object
    ) noexcept
        : object_(object) {}

    ~JsonCredentialWiper() {
        try {
            auto iterator =
                object_.find(
                    "node_credential"
                );

            if (iterator !=
                    object_.end() &&
                iterator->is_string()) {
                std::string& value =
                    iterator->get_ref<
                        std::string&
                    >();

                clear_internal_auth_token_text(
                    &value
                );

                object_.erase(iterator);
            }
        } catch (...) {
            // Best-effort wipe during stack unwinding.
        }
    }

    JsonCredentialWiper(
        const JsonCredentialWiper&
    ) = delete;

    JsonCredentialWiper& operator=(
        const JsonCredentialWiper&
    ) = delete;

private:
    json& object_;
};

bool parse_port(
    const std::string& value,
    int* output
) noexcept {
    if (!output ||
        value.empty() ||
        value.size() > 5) {
        return false;
    }

    unsigned int port = 0;

    for (unsigned char character :
         value) {
        if (character < '0' ||
            character > '9') {
            return false;
        }

        port =
            port * 10U +
            static_cast<unsigned int>(
                character - '0'
            );

        if (port > 65535U) {
            return false;
        }
    }

    if (port == 0U) {
        return false;
    }

    *output =
        static_cast<int>(port);

    return true;
}

bool safe_host_text(
    const std::string& host
) noexcept {
    if (host.empty() ||
        host.size() > 253) {
        return false;
    }

    for (unsigned char character :
         host) {
        if (character <= 0x20 ||
            character >= 0x7f ||
            character == '/' ||
            character == '\\' ||
            character == '@' ||
            character == '?' ||
            character == '#' ||
            character == '%') {
            return false;
        }
    }

    return true;
}

bool parse_https_authority(
    const std::string& base_url,
    HttpsAuthority* output
) {
    if (!output ||
        base_url.size() <= 8 ||
        base_url.size() >
            kClusterHttpsBaseUrlMaxBytes ||
        base_url.compare(
            0,
            8,
            "https://") != 0) {
        return false;
    }

    const std::string authority =
        base_url.substr(8);

    if (authority.empty() ||
        authority.find('/') !=
            std::string::npos ||
        authority.find('@') !=
            std::string::npos ||
        authority.find('?') !=
            std::string::npos ||
        authority.find('#') !=
            std::string::npos) {
        return false;
    }

    HttpsAuthority parsed;

    if (authority.front() == '[') {
        const std::size_t closing =
            authority.find(']');

        if (closing ==
                std::string::npos ||
            closing <= 1) {
            return false;
        }

        parsed.host =
            authority.substr(
                1,
                closing - 1
            );

        const std::string remainder =
            authority.substr(
                closing + 1
            );

        if (!remainder.empty()) {
            if (remainder.front() != ':' ||
                !parse_port(
                    remainder.substr(1),
                    &parsed.port)) {
                return false;
            }
        }
    } else {
        const std::size_t colon_count =
            static_cast<std::size_t>(
                std::count(
                    authority.begin(),
                    authority.end(),
                    ':'
                )
            );

        if (colon_count > 1) {
            return false;
        }

        if (colon_count == 1) {
            const std::size_t colon =
                authority.rfind(':');

            parsed.host =
                authority.substr(
                    0,
                    colon
                );

            if (!parse_port(
                    authority.substr(
                        colon + 1
                    ),
                    &parsed.port)) {
                return false;
            }
        } else {
            parsed.host =
                authority;
        }
    }

    if (!safe_host_text(
            parsed.host)) {
        return false;
    }

    *output =
        std::move(parsed);

    return true;
}

std::string trim_ascii(
    const std::string& value
) {
    std::size_t first = 0;

    while (first <
               value.size() &&
           std::isspace(
               static_cast<unsigned char>(
                   value[first]
               ))) {
        ++first;
    }

    std::size_t last =
        value.size();

    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(
                   value[last - 1]
               ))) {
        --last;
    }

    return value.substr(
        first,
        last - first
    );
}

bool content_type_is_json(
    std::string value
) {
    const std::size_t semicolon =
        value.find(';');

    if (semicolon !=
        std::string::npos) {
        value.resize(
            semicolon
        );
    }

    value =
        trim_ascii(value);

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(character)
            );
        }
    );

    return
        value ==
        "application/json";
}

bool valid_enrollment_id(
    const std::string& value
) noexcept {
    if (value.empty() ||
        value.size() >
            kMaximumEnrollmentIdBytes) {
        return false;
    }

    for (unsigned char character :
         value) {
        const bool allowed =
            (character >= 'A' &&
             character <= 'Z') ||
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9') ||
            character == '-' ||
            character == '_';

        if (!allowed) {
            return false;
        }
    }

    return true;
}

bool exact_success_body(
    const json& body,
    bool* recovered
) {
    if (!recovered ||
        !body.is_object() ||
        body.size() != 2 ||
        body.find("ok") ==
            body.end() ||
        body.find("recovered") ==
            body.end() ||
        !body["ok"].is_boolean() ||
        !body["recovered"].is_boolean() ||
        !body["ok"].get<bool>()) {
        return false;
    }

    *recovered =
        body["recovered"].get<bool>();

    return true;
}

bool exact_error_body(
    const json& body,
    std::string* error_code
) {
    if (!error_code ||
        !body.is_object() ||
        body.size() != 2 ||
        body.find("ok") ==
            body.end() ||
        body.find("error") ==
            body.end() ||
        !body["ok"].is_boolean() ||
        body["ok"].get<bool>() ||
        !body["error"].is_string()) {
        return false;
    }

    try {
        *error_code =
            body["error"].
                get<std::string>();
    } catch (...) {
        error_code->clear();
        return false;
    }

    if (error_code->empty() ||
        error_code->size() > 128) {
        error_code->clear();
        return false;
    }

    for (unsigned char character :
         *error_code) {
        const bool allowed =
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9') ||
            character == '_';

        if (!allowed) {
            error_code->clear();
            return false;
        }
    }

    return true;
}

StorageNodeEnrollmentTransportResponse
invalid_response(
    int status
) noexcept {
    return {
        StorageNodeEnrollmentTransportCode::
            InvalidResponse,
        false,
        status
    };
}

bool valid_transport_request(
    const StorageNodeEnrollmentTransportRequest&
        request,
    std::string* normalized_director
) {
    if (!normalized_director ||
        !valid_enrollment_id(
            request.enrollment_id) ||
        request.expected_generation == 0 ||
        !valid_cluster_node_id(
            request.node_id) ||
        request.protocol_version < 1 ||
        request.protocol_version > 1000 ||
        !valid_internal_auth_credential_token(
            request.authorization_token) ||
        !valid_internal_auth_credential_token(
            request.node_credential)) {
        return false;
    }

    std::string normalized_api;
    std::string normalized_tls_pin;
    std::string normalized_director_tls_pin;

    if (!normalize_node_tls_spki_sha256_pin(
            request.director_tls_spki_sha256_pin,
            &normalized_director_tls_pin,
            nullptr) ||
        normalized_director_tls_pin !=
            request.director_tls_spki_sha256_pin ||
        !normalize_node_tls_spki_sha256_pin(
            request.tls_spki_sha256_pin,
            &normalized_tls_pin,
            nullptr) ||
        normalized_tls_pin !=
            request.tls_spki_sha256_pin ||
        !normalize_cluster_https_base_url(
            request.director_url,
            normalized_director,
            nullptr) ||
        *normalized_director !=
            request.director_url ||
        !normalize_cluster_https_base_url(
            request.api_base_url,
            &normalized_api,
            nullptr) ||
        normalized_api !=
            request.api_base_url) {
        normalized_director->clear();
        return false;
    }

    return true;
}

} // namespace

StorageNodeEnrollmentTransportResponse
parse_storage_node_enrollment_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
) {
    if (body.empty() ||
        body.size() >
            kStorageNodeEnrollmentMaxResponseBytes ||
        !content_type_is_json(
            content_type)) {
        return invalid_response(
            http_status
        );
    }

    json parsed;

    try {
        parsed =
            json::parse(body);
    } catch (...) {
        return invalid_response(
            http_status
        );
    }

    if (http_status == 200) {
        bool recovered = false;

        if (!exact_success_body(
                parsed,
                &recovered)) {
            return invalid_response(
                http_status
            );
        }

        return {
            StorageNodeEnrollmentTransportCode::
                Ok,
            recovered,
            http_status
        };
    }

    std::string error_code;

    if (!exact_error_body(
            parsed,
            &error_code)) {
        return invalid_response(
            http_status
        );
    }

    switch (http_status) {
        case 401:
            if (error_code !=
                "enrollment_auth_failed") {
                return invalid_response(
                    http_status
                );
            }

            return {
                StorageNodeEnrollmentTransportCode::
                    AuthenticationFailed,
                false,
                http_status
            };

        case 409:
            return {
                StorageNodeEnrollmentTransportCode::
                    Conflict,
                false,
                http_status
            };

        case 429:
            if (error_code !=
                "rate_limited") {
                return invalid_response(
                    http_status
                );
            }

            return {
                StorageNodeEnrollmentTransportCode::
                    RateLimited,
                false,
                http_status
            };

        case 503:
            return {
                StorageNodeEnrollmentTransportCode::
                    ServiceUnavailable,
                false,
                http_status
            };

        default:
            return invalid_response(
                http_status
            );
    }
}

StorageNodeEnrollmentTransport
make_https_storage_node_enrollment_transport() {
    return [](
        const StorageNodeEnrollmentTransportRequest&
            request
    ) -> StorageNodeEnrollmentTransportResponse {
        std::string normalized_director;

        if (!valid_transport_request(
                request,
                &normalized_director)) {
            return invalid_response(0);
        }

        HttpsAuthority authority;

        if (!parse_https_authority(
                normalized_director,
                &authority)) {
            return invalid_response(0);
        }

        std::string tls_error;

        auto client =
            make_pinned_node_https_client(
                authority.host,
                authority.port,
                request
                    .director_tls_spki_sha256_pin,
                &tls_error
            );

        if (!client) {
            tls_error.clear();

            return {
                StorageNodeEnrollmentTransportCode::
                    TransportFailed,
                false,
                0
            };
        }

        tls_error.clear();

        // Security: the actual credential-bearing POST uses this exact
        // SPKI-pinned client; there is no separate unpinned connection.
        client->set_follow_location(false);
        client->set_keep_alive(false);
        client->set_decompress(false);

        client->set_connection_timeout(5, 0);
        client->set_read_timeout(8, 0);
        client->set_write_timeout(5, 0);

        json request_json{
            {
                "enrollment_id",
                request.enrollment_id
            },
            {
                "expected_generation",
                request.expected_generation
            },
            {
                "node_id",
                request.node_id
            },
            {
                "api_base_url",
                request.api_base_url
            },
            {
                "protocol_version",
                request.protocol_version
            },
            {
                "tls_spki_sha256_pin",
                request.tls_spki_sha256_pin
            },
            {
                "node_credential",
                request.node_credential
            }
        };

        JsonCredentialWiper
            request_json_wiper(
                request_json
            );

        std::string request_body;

        try {
            request_body =
                request_json.dump();
        } catch (...) {
            return invalid_response(0);
        }

        SensitiveStringWiper body_wiper(
            request_body
        );

        if (request_body.empty() ||
            request_body.size() >
                kStorageNodeEnrollmentMaxRequestBytes) {
            return invalid_response(0);
        }

        std::string authorization =
            "Bearer " +
            request.authorization_token;

        SensitiveStringWiper authorization_wiper(
            authorization
        );

        httplib::Headers headers{
            {
                "Authorization",
                authorization
            },
            {
                "Accept",
                "application/json"
            },
            {
                "User-Agent",
                "DNA-Nexus-Storage-Node-Enrollment/1"
            }
        };

        HeaderCredentialWiper header_wiper(
            headers
        );

        // Security: redirects could forward either the one-time bootstrap token
        // or the long-lived node credential to another origin. The shared
        // pinned client also keeps normal CA-chain and hostname verification.
        std::string response_body;
        SensitiveStringWiper response_wiper(
            response_body
        );

        response_body.reserve(
            512
        );

        bool too_large = false;

        const httplib::Result result =
            client->Post(
                "/internal/node/enrollments/consume",
                headers,
                request_body,
                "application/json",
                [&](
                    const char* data,
                    std::size_t length
                ) -> bool {
                    if (length >
                        kStorageNodeEnrollmentMaxResponseBytes -
                            response_body.size()) {
                        too_large = true;
                        return false;
                    }

                    response_body.append(
                        data,
                        length
                    );

                    return true;
                }
            );

        if (too_large) {
            return invalid_response(0);
        }

        if (!result) {
            return {
                StorageNodeEnrollmentTransportCode::
                    TransportFailed,
                false,
                0
            };
        }

        return
            parse_storage_node_enrollment_https_response(
                result->status,
                result->get_header_value(
                    "Content-Type"
                ),
                response_body
            );
    };
}

} // namespace pqnas::cluster
