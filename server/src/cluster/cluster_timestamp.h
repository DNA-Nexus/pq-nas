#pragma once

#include <string>

namespace pqnas::cluster {

// Converts the shared runtime clock's exact millisecond UTC representation
// into the canonical second-precision form required by cluster persistence.
//
// Security: normalize only an exact ".dddZ" suffix. All other input is left
// unchanged so existing strict validators continue to reject malformed or
// non-canonical timestamps instead of silently repairing them.
inline std::string canonicalize_cluster_runtime_timestamp(
    std::string value
) {
    const bool has_milliseconds =
        value.size() == 24 &&
        value[19] == '.' &&
        value[20] >= '0' &&
        value[20] <= '9' &&
        value[21] >= '0' &&
        value[21] <= '9' &&
        value[22] >= '0' &&
        value[22] <= '9' &&
        value[23] == 'Z';

    if (has_milliseconds) {
        value.erase(19, 4);
    }

    return value;
}

} // namespace pqnas::cluster
