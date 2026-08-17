#include "cluster/storage_user_projection_sync_https.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"

#include "storage_metadata_validation.h"
#include "storage_path_validation.h"

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
    kStorageUserProjectionSyncMaxRequestBytes =
        8u * 1024u;

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

StorageUserProjectionSyncTransportResponse
response(
    StorageUserProjectionSyncTransportCode code,
    int http_status = 0,
    std::uint64_t revision = 0
) noexcept {
    return {
        code,
        http_status,
        revision
    };
}

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

    for (unsigned char character : value) {
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

    for (unsigned char character : host) {
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

    while (first < value.size() &&
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
        value.resize(semicolon);
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

    return value ==
        "application/json";
}

bool json_u64(
    const json& value,
    std::uint64_t* output
) {
    if (!output) {
        return false;
    }

    try {
        if (value.is_number_unsigned()) {
            *output =
                value.get<std::uint64_t>();

            return true;
        }

        if (value.is_number_integer()) {
            const std::int64_t signed_value =
                value.get<std::int64_t>();

            if (signed_value < 0) {
                return false;
            }

            *output =
                static_cast<std::uint64_t>(
                    signed_value
                );

            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
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
            body["error"].get<std::string>();
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

bool valid_authorization(
    const std::string& value
) noexcept {
    constexpr char prefix[] =
        "Bearer ";

    constexpr std::size_t prefix_size =
        sizeof(prefix) - 1;

    if (value.size() !=
            prefix_size +
                kInternalAuthTokenChars ||
        value.compare(
            0,
            prefix_size,
            prefix) != 0) {
        return false;
    }

    for (std::size_t i = prefix_size;
         i < value.size();
         ++i) {
        const unsigned char character =
            static_cast<unsigned char>(
                value[i]
            );

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

bool valid_projection(
    const StorageUserProjection& projection
) {
    if (!pqnas::valid_storage_user_fingerprint_hex(
            projection.fingerprint) ||
        !valid_cluster_node_id(
            projection.home_node) ||
        projection.revision == 0) {
        return false;
    }

    if (!projection.storage_allocated) {
        return
            projection.quota_bytes == 0 &&
            projection.root_rel.empty() &&
            projection.storage_pool_id.empty();
    }

    if (projection.root_rel.empty() ||
        projection.root_rel.size() >
            kStorageUserProjectionRootRelMaxBytes ||
        !pqnas::valid_storage_pool_reference(
            projection.storage_pool_id)) {
        return false;
    }

    std::string normalized;
    std::string path_error;

    return
        pqnas::normalize_storage_relative_path_strict(
            projection.root_rel,
            &normalized,
            &path_error
        ) &&
        normalized == projection.root_rel;
}

bool valid_transport_request(
    const StorageUserProjectionSyncTransportRequest&
        request,
    std::string* normalized_url
) {
    if (!normalized_url ||
        !valid_authorization(
            request.authorization) ||
        !valid_projection(
            request.projection)) {
        return false;
    }

    if (!normalize_cluster_https_base_url(
            request.api_base_url,
            normalized_url,
            nullptr) ||
        *normalized_url !=
            request.api_base_url) {
        normalized_url->clear();
        return false;
    }

    return true;
}

} // namespace

StorageUserProjectionSyncTransportResponse
parse_storage_user_projection_sync_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body,
    std::uint64_t expected_revision
) {
    if (expected_revision == 0 ||
        body.empty() ||
        body.size() >
            kStorageUserProjectionSyncMaxResponseBytes ||
        !content_type_is_json(
            content_type)) {
        return response(
            StorageUserProjectionSyncTransportCode::
                InvalidResponse,
            http_status
        );
    }

    json parsed;

    try {
        parsed =
            json::parse(body);
    } catch (...) {
        return response(
            StorageUserProjectionSyncTransportCode::
                InvalidResponse,
            http_status
        );
    }

    if (http_status == 200) {
        if (!parsed.is_object() ||
            parsed.size() != 3 ||
            parsed.find("ok") ==
                parsed.end() ||
            parsed.find("result") ==
                parsed.end() ||
            parsed.find("revision") ==
                parsed.end() ||
            !parsed["ok"].is_boolean() ||
            !parsed["ok"].get<bool>() ||
            !parsed["result"].is_string()) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        std::string result_text;
        std::uint64_t revision = 0;

        try {
            result_text =
                parsed["result"].
                    get<std::string>();
        } catch (...) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        if (!json_u64(
                parsed["revision"],
                &revision) ||
            revision != expected_revision) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        if (result_text == "applied") {
            return response(
                StorageUserProjectionSyncTransportCode::
                    Applied,
                http_status,
                revision
            );
        }

        if (result_text == "unchanged") {
            return response(
                StorageUserProjectionSyncTransportCode::
                    Unchanged,
                http_status,
                revision
            );
        }

        return response(
            StorageUserProjectionSyncTransportCode::
                InvalidResponse,
            http_status
        );
    }

    std::string error_code;

    if (!exact_error_body(
            parsed,
            &error_code)) {
        return response(
            StorageUserProjectionSyncTransportCode::
                InvalidResponse,
            http_status
        );
    }

    switch (http_status) {
        case 400:
            if (error_code ==
                "invalid_projection") {
                return response(
                    StorageUserProjectionSyncTransportCode::
                        Rejected,
                    http_status
                );
            }

            break;

        case 401:
            // Authentication errors intentionally remain generic at the
            // protocol boundary. Do not distinguish credential parsing detail.
            return response(
                StorageUserProjectionSyncTransportCode::
                    AuthenticationFailed,
                http_status
            );

        case 409:
            if (error_code ==
                "wrong_node") {
                return response(
                    StorageUserProjectionSyncTransportCode::
                        WrongNode,
                    http_status
                );
            }

            if (error_code ==
                "stale_projection") {
                return response(
                    StorageUserProjectionSyncTransportCode::
                        Stale,
                    http_status
                );
            }

            if (error_code ==
                "projection_conflict") {
                return response(
                    StorageUserProjectionSyncTransportCode::
                        Conflict,
                    http_status
                );
            }

            break;

        case 429:
            if (error_code ==
                "rate_limited") {
                return response(
                    StorageUserProjectionSyncTransportCode::
                        RateLimited,
                    http_status
                );
            }

            break;

        case 503:
            if (error_code ==
                "projection_unavailable") {
                return response(
                    StorageUserProjectionSyncTransportCode::
                        ServiceUnavailable,
                    http_status
                );
            }

            break;

        default:
            break;
    }

    return response(
        StorageUserProjectionSyncTransportCode::
            InvalidResponse,
        http_status
    );
}

StorageUserProjectionSyncTransport
make_https_storage_user_projection_sync_transport() {
    return [](
        const StorageUserProjectionSyncTransportRequest&
            request
    ) -> StorageUserProjectionSyncTransportResponse {
        std::string normalized_url;

        if (!valid_transport_request(
                request,
                &normalized_url)) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse
            );
        }

        HttpsAuthority authority;

        if (!parse_https_authority(
                normalized_url,
                &authority)) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse
            );
        }

        const StorageUserProjection& projection =
            request.projection;

        json request_json{
            {"fingerprint", projection.fingerprint},
            {"enabled", projection.enabled},
            {
                "storage_allocated",
                projection.storage_allocated
            },
            {"quota_bytes", projection.quota_bytes},
            {"root_rel", projection.root_rel},
            {
                "storage_pool_id",
                projection.storage_pool_id
            },
            {"home_node", projection.home_node},
            {"revision", projection.revision}
        };

        std::string request_body;

        try {
            request_body =
                request_json.dump();
        } catch (...) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse
            );
        }

        SensitiveStringWiper body_wiper(
            request_body
        );

        if (request_body.empty() ||
            request_body.size() >
                kStorageUserProjectionSyncMaxRequestBytes) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse
            );
        }

        httplib::Headers headers{
            {
                "Authorization",
                request.authorization
            },
            {
                "Accept",
                "application/json"
            },
            {
                "User-Agent",
                "DNA-Nexus-Director-Projection-Sync/1"
            }
        };

        HeaderCredentialWiper header_wiper(
            headers
        );

        httplib::SSLClient client(
            authority.host,
            authority.port
        );

        if (!client.is_valid()) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    TransportFailed
            );
        }

        // Security: redirects could forward the reusable per-node bearer
        // credential to another origin.
        client.set_follow_location(false);
        client.set_keep_alive(false);
        client.set_decompress(false);

        client.set_connection_timeout(5, 0);
        client.set_read_timeout(8, 0);
        client.set_write_timeout(5, 0);

        // Security: there is deliberately no runtime insecure-TLS switch.
        client.enable_server_certificate_verification(
            true
        );

        client.enable_server_hostname_verification(
            true
        );

        std::string response_body;

        SensitiveStringWiper response_wiper(
            response_body
        );

        response_body.reserve(512);

        bool too_large = false;

        const httplib::Result result =
            client.Post(
                "/internal/storage/user-projection",
                headers,
                request_body,
                "application/json",
                [&](
                    const char* data,
                    std::size_t length
                ) -> bool {
                    if (length >
                        kStorageUserProjectionSyncMaxResponseBytes -
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
            return response(
                StorageUserProjectionSyncTransportCode::
                    InvalidResponse
            );
        }

        if (!result) {
            return response(
                StorageUserProjectionSyncTransportCode::
                    TransportFailed
            );
        }

        return
            parse_storage_user_projection_sync_https_response(
                result->status,
                result->get_header_value(
                    "Content-Type"
                ),
                response_body,
                projection.revision
            );
    };
}

} // namespace pqnas::cluster
