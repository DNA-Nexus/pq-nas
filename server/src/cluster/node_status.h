#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace pqnas {

class UsersRegistry;

namespace cluster {

inline constexpr int kNodeStatusProtocolVersionLegacy = 1;

// RT-008: protocol v2 advertises the authenticated projection range-audit
// capability. Director accepts both v1 and v2 during rolling upgrade.
inline constexpr int kNodeStatusProtocolVersionProjectionAudit = 2;

inline constexpr int kNodeStatusProtocolVersionCurrent =
    kNodeStatusProtocolVersionProjectionAudit;

// Backward-compatible name for code/tests that mean "what this binary emits".
inline constexpr int kNodeStatusProtocolVersion =
    kNodeStatusProtocolVersionCurrent;

inline constexpr bool node_status_protocol_version_supported(
    int version
) noexcept {
    return
        version == kNodeStatusProtocolVersionLegacy ||
        version == kNodeStatusProtocolVersionCurrent;
}

inline constexpr bool
node_status_protocol_supports_projection_audit(
    int version
) noexcept {
    return
        version ==
            kNodeStatusProtocolVersionProjectionAudit;
}

struct NodeStorageStatus {
    std::uint64_t total_bytes = 0;
    std::uint64_t used_bytes = 0;
    std::uint64_t free_bytes = 0;
    std::string fstype;
};

struct NodeUserStatus {
    std::uint64_t total = 0;
    std::uint64_t enabled = 0;
};

struct NodeStatusSnapshot {
    // Exact protocol observed from the peer. Locally collected Storage status
    // defaults to the current emitted protocol.
    int protocol_version =
        kNodeStatusProtocolVersionCurrent;

    std::string node_id;
    std::string hostname;
    std::string server_version;
    std::uint64_t uptime_s = 0;
    NodeStorageStatus storage;
    NodeUserStatus users;
    std::string timestamp;
};

// Collects only the narrow operational data needed by the Director.
//
// Security:
// - does not return device paths, mountpoints, process paths, IP addresses,
//   user fingerprints, usernames, or storage-root paths
// - uses the centralized trusted data root supplied by the caller
// - performs read-only host and filesystem inspection
bool collect_node_status_snapshot(
    const std::string& node_id,
    const std::string& server_version,
    const std::string& data_root,
    const UsersRegistry& users,
    NodeStatusSnapshot* out,
    std::string* err
);

nlohmann::json node_status_to_json(
    const NodeStatusSnapshot& status
);

// Strictly parses a node heartbeat response. Unknown fields, unsupported
// protocol versions, wrong roles, invalid counters and unsafe text are rejected.
bool parse_node_status_json(
    const nlohmann::json& value,
    NodeStatusSnapshot* out,
    std::string* err
);

} // namespace cluster
} // namespace pqnas
