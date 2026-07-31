#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pqnas::cluster {

struct NodeStatusSnapshot;

inline constexpr int kNodeRegistrySchemaVersion = 1;

enum class NodeRegistryInsertResult {
    Ok,
    InvalidInput,
    DuplicateNodeId,
    DuplicateApiBaseUrl,
    PersistFailed
};

const char* node_registry_insert_result_code(
    NodeRegistryInsertResult result
) noexcept;

struct NodeRegistryRecord {
    std::string node_id;
    std::string api_base_url;
    bool enabled = true;

    std::string added_at;
    std::string updated_at;

    // Director-observed state. Configuration updates do not invent health.
    std::string health = "unknown";
    std::string last_seen_at;
    std::string node_reported_at;

    int protocol_version = 0;
    std::string hostname;
    std::string server_version;
    std::uint64_t uptime_s = 0;

    std::uint64_t storage_total_bytes = 0;
    std::uint64_t storage_used_bytes = 0;
    std::uint64_t storage_free_bytes = 0;
    std::string storage_fstype;

    std::uint64_t users_total = 0;
    std::uint64_t users_enabled = 0;
};

class NodeRegistry {
public:
    // Missing file means a valid empty registry. Malformed or unsafe files
    // fail without replacing the current in-memory snapshot.
    bool load(const std::string& path, std::string* err);

    std::optional<NodeRegistryRecord> get(
        const std::string& node_id
    ) const;

    std::vector<NodeRegistryRecord> snapshot() const;

    // Creates a configured node without replacing any existing node ID or
    // normalized API URL. Conflict checks and persistence share one mutex.
    NodeRegistryInsertResult
    insert_configured_node_if_absent(
        const std::string& node_id,
        const std::string& api_base_url,
        bool enabled,
        const std::string& now_iso,
        std::string* err
    );

    // Mutations use write-through persistence: the in-memory state changes
    // only after the candidate JSON snapshot has been atomically committed.
    bool upsert_configured_node(
        const std::string& node_id,
        const std::string& api_base_url,
        bool enabled,
        const std::string& now_iso,
        std::string* err
    );

    bool set_enabled(
        const std::string& node_id,
        bool enabled,
        const std::string& now_iso,
        std::string* err
    );

    bool erase(
        const std::string& node_id,
        std::string* err
    );

    // Marks an enabled, preconfigured node offline after a failed heartbeat.
    // Existing last-seen and status data are preserved for diagnostics.
    bool mark_offline(
        const std::string& node_id,
        std::string* err,
        bool* transitioned_offline = nullptr
    );

    // Status can update only a preconfigured, enabled node. A heartbeat must
    // never create a new trusted node implicitly.
    bool apply_status(
        const NodeStatusSnapshot& status,
        const std::string& received_at,
        std::string* err,
        bool* transitioned_online = nullptr
    );

private:
    // Persists and installs one candidate while the registry mutex is held.
    // If rename succeeds but directory fsync fails, memory is still advanced to
    // the committed inode before the mutation reports failure.
    bool commit_candidate_locked(
        std::unordered_map<
            std::string,
            NodeRegistryRecord
        >&& candidate,
        std::string* err
    );

    mutable std::mutex mutex_;
    std::filesystem::path path_;

    std::unordered_map<
        std::string,
        NodeRegistryRecord
    > nodes_;
};

} // namespace pqnas::cluster
