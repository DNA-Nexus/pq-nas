#include "node_heartbeat_https.h"

#include "internal_auth.h"
#include "node_registry.h"

#include "httplib.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>

namespace pqnas::cluster {
namespace {

struct HttpsAuthority {
    std::string host;
    int port = 443;
};

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

bool parse_port(
    const std::string& value,
    int* out
) {
    if (!out ||
        value.empty() ||
        value.size() > 5) {
        return false;
    }

    unsigned int port = 0;

    for (unsigned char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }

        port =
            port * 10U +
            static_cast<unsigned int>(ch - '0');

        if (port > 65535U) {
            return false;
        }
    }

    if (port == 0U) {
        return false;
    }

    *out = static_cast<int>(port);
    return true;
}

bool safe_host_text(
    const std::string& host
) {
    if (host.empty() || host.size() > 253) {
        return false;
    }

    for (unsigned char ch : host) {
        if (ch <= 0x20 ||
            ch >= 0x7f ||
            ch == '/' ||
            ch == '\\' ||
            ch == '@' ||
            ch == '?' ||
            ch == '#' ||
            ch == '%') {
            return false;
        }
    }

    return true;
}

bool parse_https_authority(
    const std::string& base_url,
    HttpsAuthority* out
) {
    if (!out ||
        base_url.size() <= 8 ||
        base_url.size() > 2048 ||
        base_url.compare(0, 8, "https://") != 0) {
        return false;
    }

    const std::string authority =
        base_url.substr(8);

    if (authority.empty() ||
        authority.find('/') != std::string::npos ||
        authority.find('@') != std::string::npos ||
        authority.find('?') != std::string::npos ||
        authority.find('#') != std::string::npos) {
        return false;
    }

    HttpsAuthority parsed;

    if (authority.front() == '[') {
        const std::size_t closing =
            authority.find(']');

        if (closing == std::string::npos ||
            closing <= 1) {
            return false;
        }

        parsed.host =
            authority.substr(1, closing - 1);

        const std::string remainder =
            authority.substr(closing + 1);

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
                authority.substr(0, colon);

            if (!parse_port(
                    authority.substr(colon + 1),
                    &parsed.port)) {
                return false;
            }
        } else {
            parsed.host = authority;
        }
    }

    if (!safe_host_text(parsed.host)) {
        return false;
    }

    *out = std::move(parsed);
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

    std::size_t last = value.size();

    while (last > first &&
           std::isspace(
               static_cast<unsigned char>(
                   value[last - 1]
               ))) {
        --last;
    }

    return value.substr(first, last - first);
}

bool content_type_is_json(
    std::string value
) {
    const std::size_t semicolon =
        value.find(';');

    if (semicolon != std::string::npos) {
        value.resize(semicolon);
    }

    value = trim_ascii(value);

    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return static_cast<char>(
                std::tolower(ch)
            );
        }
    );

    return value == "application/json";
}

} // namespace

NodeHeartbeatFetch make_https_node_heartbeat_fetch() {
    return [](
        const NodeRegistryRecord& node,
        const std::string& authorization,
        std::string* response_body,
        std::string* error
    ) -> bool {
        if (error) {
            error->clear();
        }

        if (!response_body) {
            set_error(
                error,
                "heartbeat response buffer is missing"
            );
            return false;
        }

        response_body->clear();

        if (authorization.size() !=
                sizeof("Bearer ") - 1 +
                kInternalAuthTokenChars ||
            authorization.compare(
                0,
                sizeof("Bearer ") - 1,
                "Bearer ") != 0) {
            set_error(
                error,
                "heartbeat authorization is invalid"
            );
            return false;
        }

        HttpsAuthority authority;

        if (!parse_https_authority(
                node.api_base_url,
                &authority)) {
            set_error(
                error,
                "heartbeat URL is invalid"
            );
            return false;
        }

        httplib::SSLClient client(
            authority.host,
            authority.port
        );

        if (!client.is_valid()) {
            set_error(
                error,
                "heartbeat TLS client initialization failed"
            );
            return false;
        }

        // Security: never follow redirects because they could forward the
        // cluster bearer credential to another origin.
        client.set_follow_location(false);
        client.set_keep_alive(false);
        client.set_decompress(false);

        client.set_connection_timeout(5, 0);
        client.set_read_timeout(8, 0);
        client.set_write_timeout(5, 0);

        // Security: both certificate-chain and hostname verification remain
        // enabled. There is deliberately no insecure configuration switch.
        client.enable_server_certificate_verification(true);
        client.enable_server_hostname_verification(true);

        const httplib::Headers headers{
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
                "DNA-Nexus-Director-Heartbeat/1"
            }
        };

        std::string body;
        body.reserve(4096);

        bool too_large = false;

        const httplib::Result result =
            client.Get(
                "/internal/node/status",
                headers,
                [&](
                    const char* data,
                    std::size_t length
                ) -> bool {
                    if (length >
                            kNodeHeartbeatMaxResponseBytes -
                            body.size()) {
                        too_large = true;
                        return false;
                    }

                    body.append(data, length);
                    return true;
                }
            );

        if (too_large) {
            set_error(
                error,
                "heartbeat response is too large"
            );
            return false;
        }

        if (!result) {
            set_error(
                error,
                "heartbeat HTTPS request failed"
            );
            return false;
        }

        if (result->status != 200) {
            set_error(
                error,
                "heartbeat returned unexpected HTTP status"
            );
            return false;
        }

        if (!content_type_is_json(
                result->get_header_value(
                    "Content-Type"
                ))) {
            set_error(
                error,
                "heartbeat returned unexpected content type"
            );
            return false;
        }

        if (body.empty()) {
            set_error(
                error,
                "heartbeat returned an empty response"
            );
            return false;
        }

        response_body->swap(body);
        return true;
    };
}

} // namespace pqnas::cluster
