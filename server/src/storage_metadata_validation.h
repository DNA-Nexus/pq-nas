#pragma once

#include <string>

namespace pqnas {

// Stable managed-pool ID syntax.
//
// Security: keep pool identifiers narrow so control-plane metadata cannot be
// interpreted as filesystem paths, URLs, shell fragments, or display labels.
inline bool valid_managed_storage_pool_id(
    const std::string& value
) {
    if (value.size() < 2 || value.size() > 24) {
        return false;
    }

    for (unsigned char ch : value) {
        const bool valid =
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '_';

        if (!valid) {
            return false;
        }
    }

    return true;
}

// Empty is the persisted representation of the implicit default pool.
// Non-empty values must be valid managed-pool IDs.
inline bool valid_storage_pool_reference(
    const std::string& value
) {
    return value.empty() ||
           valid_managed_storage_pool_id(value);
}

// User identity fingerprints are SHA3-512 rendered as lowercase hex.
//
// Security: reject alternate encodings, uppercase aliases and malformed
// identities before they can become persistent Storage projection keys.
inline bool valid_storage_user_fingerprint_hex(
    const std::string& value
) {
    if (value.size() != 128) {
        return false;
    }

    for (unsigned char ch : value) {
        const bool valid =
            (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f');

        if (!valid) {
            return false;
        }
    }

    return true;
}

} // namespace pqnas
