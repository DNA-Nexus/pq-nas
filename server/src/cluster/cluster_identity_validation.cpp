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

} // namespace pqnas::cluster
