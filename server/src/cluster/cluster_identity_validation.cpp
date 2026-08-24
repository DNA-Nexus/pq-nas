#include "cluster/cluster_identity_validation.h"

#include <utility>

namespace pqnas::cluster {
namespace {

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

bool ascii_alphanumeric(
    unsigned char ch
) {
    return
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9');
}

bool parse_cluster_https_port(
    const std::string& value,
    int* output
) {
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

bool safe_cluster_https_host_text(
    const std::string& host
) {
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
            character == '%' ||
            character == '[' ||
            character == ']') {
            return false;
        }
    }

    return true;
}

} // namespace

bool valid_cluster_node_id(
    const std::string& value
) {
    if (value.empty() ||
        value.size() > kClusterNodeIdMaxBytes) {
        return false;
    }

    for (std::size_t index = 0;
         index < value.size();
         ++index) {
        const unsigned char ch =
            static_cast<unsigned char>(
                value[index]
            );

        if (index == 0) {
            if (!ascii_alphanumeric(ch)) {
                return false;
            }

            continue;
        }

        if (!ascii_alphanumeric(ch) &&
            ch != '-' &&
            ch != '_' &&
            ch != '.') {
            return false;
        }
    }

    return true;
}

bool normalize_cluster_https_base_url(
    const std::string& raw,
    std::string* normalized,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!normalized) {
        set_error(
            error,
            "missing cluster HTTPS base URL output"
        );
        return false;
    }

    if (raw.empty() ||
        raw.size() > kClusterHttpsBaseUrlMaxBytes) {
        set_error(
            error,
            "invalid cluster HTTPS base URL size"
        );
        return false;
    }

    // Security: reject surrounding whitespace and non-ASCII input rather than
    // silently changing the authority bound into control-plane state.
    for (unsigned char ch : raw) {
        if (ch < 0x21 || ch > 0x7e) {
            set_error(
                error,
                "cluster HTTPS base URL contains invalid characters"
            );
            return false;
        }
    }

    constexpr char kScheme[] = "https://";
    constexpr std::size_t kSchemeLength =
        sizeof(kScheme) - 1;

    if (raw.compare(
            0,
            kSchemeLength,
            kScheme) != 0) {
        set_error(
            error,
            "cluster base URL must use HTTPS"
        );
        return false;
    }

    std::string candidate = raw;

    if (candidate.size() > kSchemeLength &&
        candidate.back() == '/') {
        candidate.pop_back();
    }

    const std::string authority =
        candidate.substr(kSchemeLength);

    // Security: only a base authority is accepted. Paths and encoded delimiter
    // tricks could otherwise produce inconsistent endpoint interpretation.
    if (authority.empty() ||
        authority.front() == ':' ||
        authority.back() == ':' ||
        authority.find('/') != std::string::npos ||
        authority.find('\\') != std::string::npos ||
        authority.find('@') != std::string::npos ||
        authority.find('?') != std::string::npos ||
        authority.find('#') != std::string::npos ||
        authority.find('%') != std::string::npos) {
        set_error(
            error,
            "cluster URL must be a base HTTPS authority"
        );
        return false;
    }

    *normalized = std::move(candidate);
    return true;
}

bool parse_cluster_https_authority(
    const std::string& raw,
    ClusterHttpsAuthority* authority,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!authority) {
        set_error(
            error,
            "missing cluster HTTPS authority output"
        );
        return false;
    }

    std::string normalized;

    if (!normalize_cluster_https_base_url(
            raw,
            &normalized,
            error)) {
        return false;
    }

    constexpr std::size_t kSchemeLength =
        sizeof("https://") - 1;

    const std::string value =
        normalized.substr(
            kSchemeLength
        );

    ClusterHttpsAuthority parsed;

    if (!value.empty() &&
        value.front() == '[') {
        const std::size_t closing =
            value.find(']');

        if (closing == std::string::npos ||
            closing <= 1) {
            set_error(
                error,
                "invalid bracketed cluster HTTPS authority"
            );
            return false;
        }

        parsed.host =
            value.substr(
                1,
                closing - 1
            );

        const std::string remainder =
            value.substr(
                closing + 1
            );

        if (!remainder.empty()) {
            if (remainder.front() != ':' ||
                !parse_cluster_https_port(
                    remainder.substr(1),
                    &parsed.port)) {
                set_error(
                    error,
                    "invalid cluster HTTPS authority port"
                );
                return false;
            }
        }
    } else {
        const std::size_t first_colon =
            value.find(':');

        const std::size_t last_colon =
            value.rfind(':');

        if (first_colon !=
                std::string::npos &&
            first_colon !=
                last_colon) {
            // Security: IPv6 literals must use brackets so host/port
            // interpretation is identical in every cluster transport.
            set_error(
                error,
                "unbracketed IPv6 cluster HTTPS authority"
            );
            return false;
        }

        if (last_colon ==
            std::string::npos) {
            parsed.host = value;
        } else {
            parsed.host =
                value.substr(
                    0,
                    last_colon
                );

            if (!parse_cluster_https_port(
                    value.substr(
                        last_colon + 1
                    ),
                    &parsed.port)) {
                set_error(
                    error,
                    "invalid cluster HTTPS authority port"
                );
                return false;
            }
        }
    }

    if (!safe_cluster_https_host_text(
            parsed.host)) {
        set_error(
            error,
            "invalid cluster HTTPS authority host"
        );
        return false;
    }

    *authority =
        std::move(parsed);

    return true;
}


} // namespace pqnas::cluster
