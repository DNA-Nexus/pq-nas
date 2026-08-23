#include "cluster/node_credential_rotation_https.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"

#include "httplib.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

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

void clear_json_credential(
    json* object
) noexcept {
    if (!object ||
        !object->is_object()) {
        return;
    }

    try {
        auto iterator =
            object->find(
                "node_credential"
            );

        if (iterator !=
                object->end() &&
            iterator->is_string()) {
            std::string& credential =
                iterator->get_ref<
                    std::string&
                >();

            clear_internal_auth_token_text(
                &credential
            );
        }

        object->erase(
            "node_credential"
        );
    } catch (...) {
        // Security: best-effort secret cleanup only. Never log parser state.
    }
}

class JsonCredentialWiper {
public:
    explicit JsonCredentialWiper(
        json& value
    ) noexcept
        : value_(value) {}

    ~JsonCredentialWiper() {
        clear_json_credential(
            &value_
        );
    }

    JsonCredentialWiper(
        const JsonCredentialWiper&
    ) = delete;

    JsonCredentialWiper& operator=(
        const JsonCredentialWiper&
    ) = delete;

private:
    json& value_;
};

NodeCredentialRotationPrepareResponse
prepare_response(
    NodeCredentialRotationTransportCode code,
    int http_status = 0
) noexcept {
    NodeCredentialRotationPrepareResponse
        response;

    response.code =
        code;

    response.http_status =
        http_status;

    return response;
}

NodeCredentialRotationFinalizeResponse
finalize_response(
    NodeCredentialRotationTransportCode code,
    int http_status = 0
) noexcept {
    return {
        code,
        http_status
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
        static_cast<int>(
            port
        );

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
            // IPv6 literals must be bracketed.
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
        std::move(
            parsed
        );

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
        trim_ascii(
            value
        );

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character) {
            return static_cast<char>(
                std::tolower(
                    character
                )
            );
        }
    );

    return
        value ==
        "application/json";
}

bool valid_authorization(
    const std::string& value
) noexcept {
    constexpr char prefix[] =
        "Bearer ";

    constexpr std::size_t prefix_bytes =
        sizeof(prefix) - 1;

    if (value.size() !=
            prefix_bytes +
                kInternalAuthTokenChars ||
        value.compare(
            0,
            prefix_bytes,
            prefix) != 0) {
        return false;
    }

    for (std::size_t index =
             prefix_bytes;
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

bool valid_transport_request(
    const std::string& api_base_url,
    const std::string& authorization,
    HttpsAuthority* authority
) {
    if (!authority ||
        !valid_authorization(
            authorization)) {
        return false;
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            api_base_url,
            &normalized_url,
            nullptr) ||
        normalized_url !=
            api_base_url) {
        return false;
    }

    return
        parse_https_authority(
            normalized_url,
            authority
        );
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
        error_code->size() > 64) {
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

NodeCredentialRotationTransportCode
map_error_response(
    int http_status,
    const json& body
) {
    std::string error_code;

    if (!exact_error_body(
            body,
            &error_code)) {
        return
            NodeCredentialRotationTransportCode::
                InvalidResponse;
    }

    if (http_status == 401 &&
        error_code ==
            "unauthorized") {
        return
            NodeCredentialRotationTransportCode::
                AuthenticationFailed;
    }

    if (http_status == 429 &&
        error_code ==
            "rate_limited") {
        return
            NodeCredentialRotationTransportCode::
                RateLimited;
    }

    if (http_status == 503 &&
        error_code ==
            "rotation_unavailable") {
        return
            NodeCredentialRotationTransportCode::
                ServiceUnavailable;
    }

    return
        NodeCredentialRotationTransportCode::
            InvalidResponse;
}

template <typename Response>
Response parse_common_failure(
    int http_status,
    const json& parsed
);

template <>
NodeCredentialRotationPrepareResponse
parse_common_failure<
    NodeCredentialRotationPrepareResponse
>(
    int http_status,
    const json& parsed
) {
    return prepare_response(
        map_error_response(
            http_status,
            parsed
        ),
        http_status
    );
}

template <>
NodeCredentialRotationFinalizeResponse
parse_common_failure<
    NodeCredentialRotationFinalizeResponse
>(
    int http_status,
    const json& parsed
) {
    return finalize_response(
        map_error_response(
            http_status,
            parsed
        ),
        http_status
    );
}

template <typename ParsedResponse>
ParsedResponse perform_post(
    const std::string& api_base_url,
    const std::string& authorization,
    const char* path,
    const std::function<
        ParsedResponse(
            int,
            const std::string&,
            const std::string&
        )
    >& parser,
    const std::function<
        ParsedResponse(
            NodeCredentialRotationTransportCode,
            int
        )
    >& failure_factory
) {
    HttpsAuthority authority;

    if (!valid_transport_request(
            api_base_url,
            authorization,
            &authority)) {
        return failure_factory(
            NodeCredentialRotationTransportCode::
                InvalidResponse,
            0
        );
    }

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
            "DNA-Nexus-Director-Credential-Rotation/1"
        }
    };

    HeaderCredentialWiper
        header_wiper(
            headers
        );

    httplib::SSLClient client(
        authority.host,
        authority.port
    );

    if (!client.is_valid()) {
        return failure_factory(
            NodeCredentialRotationTransportCode::
                TransportFailed,
            0
        );
    }

    // Security: never allow redirects to forward a reusable cluster bearer to
    // another origin.
    client.set_follow_location(false);
    client.set_keep_alive(false);
    client.set_decompress(false);

    client.set_connection_timeout(
        5,
        0
    );

    client.set_read_timeout(
        8,
        0
    );

    client.set_write_timeout(
        5,
        0
    );

    // Security: there is deliberately no insecure TLS runtime switch.
    client.enable_server_certificate_verification(
        true
    );

    client.enable_server_hostname_verification(
        true
    );

    std::string response_body;

    SensitiveStringWiper
        response_wiper(
            response_body
        );

    response_body.reserve(
        256
    );

    bool too_large = false;

    const httplib::Result result =
        client.Post(
            path,
            headers,
            "",
            "application/json",
            [&](
                const char* data,
                std::size_t length
            ) -> bool {
                if (length >
                    kNodeCredentialRotationHttpsMaxResponseBytes -
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
        return failure_factory(
            NodeCredentialRotationTransportCode::
                InvalidResponse,
            0
        );
    }

    if (!result) {
        return failure_factory(
            NodeCredentialRotationTransportCode::
                TransportFailed,
            0
        );
    }

    return parser(
        result->status,
        result->get_header_value(
            "Content-Type"
        ),
        response_body
    );
}

} // namespace

NodeCredentialRotationPrepareResponse
parse_node_credential_rotation_prepare_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
) {
    if (body.empty() ||
        body.size() >
            kNodeCredentialRotationHttpsMaxResponseBytes ||
        !content_type_is_json(
            content_type)) {
        return prepare_response(
            NodeCredentialRotationTransportCode::
                InvalidResponse,
            http_status
        );
    }

    json parsed;

    try {
        parsed =
            json::parse(
                body
            );
    } catch (...) {
        return prepare_response(
            NodeCredentialRotationTransportCode::
                InvalidResponse,
            http_status
        );
    }

    JsonCredentialWiper
        parsed_wiper(
            parsed
        );

    if (http_status == 200) {
        if (!parsed.is_object() ||
            parsed.size() != 2 ||
            parsed.find("ok") ==
                parsed.end() ||
            parsed.find("node_credential") ==
                parsed.end() ||
            !parsed["ok"].is_boolean() ||
            !parsed["ok"].get<bool>() ||
            !parsed["node_credential"].
                is_string()) {
            return prepare_response(
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        NodeCredentialRotationPrepareResponse
            response =
                prepare_response(
                    NodeCredentialRotationTransportCode::
                        Ok,
                    http_status
                );

        try {
            response.pending_credential =
                parsed["node_credential"].
                    get<std::string>();
        } catch (...) {
            return prepare_response(
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        if (!valid_internal_auth_credential_token(
                response.pending_credential)) {
            clear_internal_auth_token_text(
                &response.pending_credential
            );

            return prepare_response(
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        return response;
    }

    return
        parse_common_failure<
            NodeCredentialRotationPrepareResponse
        >(
            http_status,
            parsed
        );
}

NodeCredentialRotationFinalizeResponse
parse_node_credential_rotation_finalize_https_response(
    int http_status,
    const std::string& content_type,
    const std::string& body
) {
    if (body.empty() ||
        body.size() >
            kNodeCredentialRotationHttpsMaxResponseBytes ||
        !content_type_is_json(
            content_type)) {
        return finalize_response(
            NodeCredentialRotationTransportCode::
                InvalidResponse,
            http_status
        );
    }

    json parsed;

    try {
        parsed =
            json::parse(
                body
            );
    } catch (...) {
        return finalize_response(
            NodeCredentialRotationTransportCode::
                InvalidResponse,
            http_status
        );
    }

    JsonCredentialWiper
        parsed_wiper(
            parsed
        );

    if (http_status == 200) {
        if (!parsed.is_object() ||
            parsed.size() != 1 ||
            parsed.find("ok") ==
                parsed.end() ||
            !parsed["ok"].is_boolean() ||
            !parsed["ok"].get<bool>()) {
            return finalize_response(
                NodeCredentialRotationTransportCode::
                    InvalidResponse,
                http_status
            );
        }

        return finalize_response(
            NodeCredentialRotationTransportCode::
                Ok,
            http_status
        );
    }

    return
        parse_common_failure<
            NodeCredentialRotationFinalizeResponse
        >(
            http_status,
            parsed
        );
}

NodeCredentialRotationPrepareTransport
make_https_node_credential_rotation_prepare_transport() {
    return [](
        const NodeCredentialRotationPrepareRequest&
            request
    ) {
        return perform_post<
            NodeCredentialRotationPrepareResponse
        >(
            request.api_base_url,
            request.authorization,
            "/internal/node/credential/rotation/prepare",
            [](
                int status,
                const std::string& content_type,
                const std::string& body
            ) {
                return
                    parse_node_credential_rotation_prepare_https_response(
                        status,
                        content_type,
                        body
                    );
            },
            [](
                NodeCredentialRotationTransportCode code,
                int status
            ) {
                return prepare_response(
                    code,
                    status
                );
            }
        );
    };
}

NodeCredentialRotationFinalizeTransport
make_https_node_credential_rotation_finalize_transport() {
    return [](
        const NodeCredentialRotationFinalizeRequest&
            request
    ) {
        return perform_post<
            NodeCredentialRotationFinalizeResponse
        >(
            request.api_base_url,
            request.authorization,
            "/internal/node/credential/rotation/finalize",
            [](
                int status,
                const std::string& content_type,
                const std::string& body
            ) {
                return
                    parse_node_credential_rotation_finalize_https_response(
                        status,
                        content_type,
                        body
                    );
            },
            [](
                NodeCredentialRotationTransportCode code,
                int status
            ) {
                return finalize_response(
                    code,
                    status
                );
            }
        );
    };
}

} // namespace pqnas::cluster
