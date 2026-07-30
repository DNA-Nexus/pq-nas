#pragma once

#include <cstddef>
#include <string>

namespace pqnas::cluster {

inline constexpr std::size_t
    kClusterNodeIdMaxBytes = 64;

inline constexpr std::size_t
    kClusterHttpsBaseUrlMaxBytes = 2048;

// Validates the stable node identifier used in role configuration, registry
// keys, heartbeat messages, audit fields and future enrollment records.
//
// Security: the identifier is strict ASCII and is never trimmed or otherwise
// canonicalized implicitly. This prevents different trust-boundary components
// from binding different raw values to the same apparent node.
bool valid_cluster_node_id(
    const std::string& value
);

// Validates and canonicalizes an HTTPS control-plane base authority.
//
// Accepted canonicalization:
// - one optional trailing slash is removed
//
// Rejected:
// - plaintext HTTP
// - whitespace, control bytes and non-ASCII bytes
// - userinfo, paths, queries, fragments and encoded delimiter syntax
// - empty authorities and malformed leading/trailing port separators
// - inputs exceeding the bounded control-plane URL size
//
// The output is replaced only after every validation step succeeds.
bool normalize_cluster_https_base_url(
    const std::string& raw,
    std::string* normalized,
    std::string* error
);

} // namespace pqnas::cluster
