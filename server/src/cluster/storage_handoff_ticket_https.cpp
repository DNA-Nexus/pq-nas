#include "cluster/storage_handoff_ticket_https.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_tls_https_client.h"
#include "cluster/node_tls_identity.h"
#include "cluster/storage_handoff_ticket_store.h"
#include "cluster/strict_json_parse.h"

#include "storage_metadata_validation.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kStorageHandoffTicketMaxRequestBytes =
        1024u;

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

class TicketStringWiper {
public:
    explicit TicketStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~TicketStringWiper() {
        clear_storage_handoff_ticket_text(
            &value_
        );
    }

    TicketStringWiper(
        const TicketStringWiper&
    ) = delete;

    TicketStringWiper& operator=(
        const TicketStringWiper&
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

StorageHandoffTicketTransportResponse
response(
    StorageHandoffTicketTransportCode code,
    int http_status = 0,
    std::string ticket = {},
    std::int64_t expires_at = 0
) {
    StorageHandoffTicketTransportResponse
        result;

    result.code = code;
    result.http_status = http_status;
    result.ticket = std::move(ticket);
    result.expires_at = expires_at;

    return result;
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

    for (std::size_t index = prefix_size;
         index < value.size();
         ++index) {
        const unsigned char character =
            static_cast<unsigned char>(
                value[index]
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

bool json_positive_i64(
    const json& value,
    std::int64_t* output
) {
    if (!output) {
        return false;
    }

    try {
        if (value.is_number_unsigned()) {
            const std::uint64_t number =
                value.get<std::uint64_t>();

            if (number == 0 ||
                number >
                    static_cast<std::uint64_t>(
                        std::numeric_limits<
                            std::int64_t
                        >::max())) {
                return false;
            }

            *output =
                static_cast<std::int64_t>(
                    number
                );

            return true;
        }

        if (value.is_number_integer()) {
            const std::int64_t number =
                value.get<std::int64_t>();

            if (number <= 0) {
                return false;
            }

            *output = number;
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

bool valid_transport_request(
    const StorageHandoffTicketTransportRequest&
        request,
    std::string* normalized_url
) {
    std::string normalized_tls_pin;

    if (!normalized_url ||
        !normalize_node_tls_spki_sha256_pin(
            request.tls_spki_sha256_pin,
            &normalized_tls_pin,
            nullptr) ||
        normalized_tls_pin !=
            request.tls_spki_sha256_pin ||
        !valid_authorization(
            request.authorization) ||
        !pqnas::valid_storage_user_fingerprint_hex(
            request.fingerprint) ||
        !valid_storage_handoff_browser_binding_hash(
            request.browser_binding_hash)) {
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

StorageHandoffTicketTransportResponse
parse_storage_handoff_ticket_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
) {
    if (body.empty() ||
        body.size() >
            kStorageHandoffTicketMaxResponseBytes ||
        !content_type_is_json(
            content_type)) {
        return response(
            StorageHandoffTicketTransportCode::
                InvalidResponse,
            http_status
        );
    }

    json parsed;

    if (!parse_json_reject_duplicate_keys(
            body,
            &parsed)) {
        return response(
            StorageHandoffTicketTransportCode::
                InvalidResponse,
            http_status
        );
    }

    if (http_status == 200) {
        if (!parsed.is_object() ||
            parsed.size() != 3 ||
            parsed.find("ok") ==
                parsed.end() ||
            parsed.find("ticket") ==
                parsed.end() ||
            parsed.find("expires_at") ==
                parsed.end() ||
            !parsed["ok"].is_boolean() ||
            !parsed["ok"].get<bool>() ||
            !parsed["ticket"].is_string()) {
            return response(
                StorageHandoffTicketTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        std::string ticket;
        TicketStringWiper
            ticket_wiper(ticket);

        std::int64_t expires_at = 0;

        try {
            std::string& parsed_ticket =
                parsed["ticket"].
                    get_ref<std::string&>();

            TicketStringWiper
                parsed_ticket_wiper(
                    parsed_ticket
                );

            ticket =
                parsed_ticket;
        } catch (...) {
            return response(
                StorageHandoffTicketTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        if (!valid_storage_handoff_ticket_text(ticket) ||
            !json_positive_i64(
                parsed["expires_at"],
                &expires_at)) {
            clear_storage_handoff_ticket_text(
                &ticket
            );

            return response(
                StorageHandoffTicketTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        return response(
            StorageHandoffTicketTransportCode::Ok,
            http_status,
            std::move(ticket),
            expires_at
        );
    }

    std::string error_code;

    if (!exact_error_body(
            parsed,
            &error_code)) {
        return response(
            StorageHandoffTicketTransportCode::
                InvalidResponse,
            http_status
        );
    }

    switch (http_status) {
        case 400:
            if (error_code ==
                "bad_request") {
                return response(
                    StorageHandoffTicketTransportCode::
                        Rejected,
                    http_status
                );
            }
            break;

        case 401:
            if (error_code ==
                "unauthorized") {
                return response(
                    StorageHandoffTicketTransportCode::
                        AuthenticationFailed,
                    http_status
                );
            }
            break;

        case 403:
            if (error_code ==
                "user_not_authorized") {
                return response(
                    StorageHandoffTicketTransportCode::
                        UserNotAuthorized,
                    http_status
                );
            }
            break;

        case 409:
            if (error_code ==
                "handoff_restart_required") {
                return response(
                    StorageHandoffTicketTransportCode::
                        RestartRequired,
                    http_status
                );
            }
            break;

        case 429:
            if (error_code ==
                "rate_limited") {
                return response(
                    StorageHandoffTicketTransportCode::
                        RateLimited,
                    http_status
                );
            }
            break;

        case 503:
            if (error_code ==
                "handoff_unavailable") {
                return response(
                    StorageHandoffTicketTransportCode::
                        ServiceUnavailable,
                    http_status
                );
            }
            break;

        default:
            break;
    }

    return response(
        StorageHandoffTicketTransportCode::
            InvalidResponse,
        http_status
    );
}

StorageHandoffTicketTransport
make_https_storage_handoff_ticket_transport() {
    return [](
        const StorageHandoffTicketTransportRequest&
            request
    ) -> StorageHandoffTicketTransportResponse {
        std::string normalized_url;

        if (!valid_transport_request(
                request,
                &normalized_url)) {
            return response(
                StorageHandoffTicketTransportCode::
                    InvalidResponse
            );
        }

        HttpsAuthority authority;

        if (!parse_https_authority(
                normalized_url,
                &authority)) {
            return response(
                StorageHandoffTicketTransportCode::
                    InvalidResponse
            );
        }

        // Security: the Director supplies only identity + browser verifier.
        // Storage itself resolves home_node and projection revision.
        json request_json{
            {
                "fingerprint",
                request.fingerprint
            },
            {
                "browser_binding_hash",
                request.browser_binding_hash
            }
        };

        std::string request_body;

        try {
            request_body =
                request_json.dump();
        } catch (...) {
            return response(
                StorageHandoffTicketTransportCode::
                    InvalidResponse
            );
        }

        SensitiveStringWiper
            body_wiper(request_body);

        if (request_body.empty() ||
            request_body.size() >
                kStorageHandoffTicketMaxRequestBytes) {
            return response(
                StorageHandoffTicketTransportCode::
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
                "DNA-Nexus-Director-Handoff-Ticket/1"
            }
        };

        HeaderCredentialWiper
            header_wiper(headers);

        // Security: never create an unpinned authenticated SSL client.
        auto client =
            make_pinned_node_https_client(
                authority.host,
                authority.port,
                request.tls_spki_sha256_pin,
                nullptr
            );

        if (!client) {
            return response(
                StorageHandoffTicketTransportCode::
                    TransportFailed
            );
        }

        // Security: redirects must never receive the reusable per-node bearer.
        client->set_follow_location(false);
        client->set_keep_alive(false);
        client->set_decompress(false);

        client->set_connection_timeout(5, 0);
        client->set_read_timeout(8, 0);
        client->set_write_timeout(5, 0);

        std::string response_body;

        // Security: successful response contains a plaintext one-time ticket.
        SensitiveStringWiper
            response_wiper(response_body);

        response_body.reserve(512);

        bool too_large = false;

        const httplib::Result result =
            client->Post(
                "/internal/storage/handoff/ticket",
                headers,
                request_body,
                "application/json",
                [&](
                    const char* data,
                    std::size_t length
                ) -> bool {
                    if (length >
                        kStorageHandoffTicketMaxResponseBytes -
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
                StorageHandoffTicketTransportCode::
                    InvalidResponse
            );
        }

        if (!result) {
            return response(
                StorageHandoffTicketTransportCode::
                    TransportFailed
            );
        }

        return
            parse_storage_handoff_ticket_https_response(
                result->status,
                result->get_header_value(
                    "Content-Type"
                ),
                response_body
            );
    };
}

} // namespace pqnas::cluster
