#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <mutex>
#include <optional>
#include <vector>
#include <cstdint>
#include <functional>

namespace pqnas {

// Empty means local/standalone placement. Non-empty values are stable cluster
// node IDs, never hostnames, URLs, filesystem paths or display labels.
bool valid_user_home_node_id(
    const std::string& value
);

// Empty means no placement-policy override. Non-empty values are stable
// Director-managed policy IDs, never paths, URLs or display labels.
bool valid_user_placement_policy_id(
    const std::string& value
);

struct UserRec {
    std::string fingerprint;   // hex (identity)

    // Existing fields
    std::string name;
    std::string role;          // "admin" | "user"
    std::string status;        // "enabled" | "disabled" | "revoked"
    std::string added_at;      // ISO-8601 UTC
    std::string last_seen;     // ISO-8601 UTC
    std::string notes;
    std::string avatar_url;

    // User preferences persisted across browsers/devices.
    // Stored as a compact JSON object string; schema is validated by callers.
    std::string app_prefs_json;

    // Vault Master recovery metadata for this exact user fingerprint.
    // Security: only public key material is stored here. The private Master
    // recovery key is generated client-side and must never be sent to the server.
    std::string vault_master_recovery_status; // "active" | "disabled" | "not_configured"
    std::string vault_master_recovery_public_key_b64;
    std::string vault_master_recovery_public_key_sha256;
    long long vault_master_recovery_created_at = 0;
    std::string vault_master_recovery_label;

    // New: admin profile metadata (non-security)
    std::string group;         // e.g. "Family", "Work"
    std::string email;
    std::string address;

    // New: storage metadata (Option A; metadata only for now)
    std::string storage_state; // "unallocated" | "allocated"
    std::uint64_t quota_bytes = 0;
    std::string root_rel;      // e.g. "users/<fingerprint>"
    std::string storage_set_at; // ISO-8601 UTC
    std::string storage_set_by; // actor fingerprint (hex)
    std::string storage_pool_id; // "" = default pool

    // Cluster placement metadata.
    //
    // ""        = local/standalone user
    // "node-01" = stable Director-managed node ID
    std::string home_node;

    // Monotonic Director-owned version of the Storage Node projection.
    //
    // 0 is the backward-compatible value for users loaded from an older
    // users.json that predates projection revisions. Once a persisted
    // projection mutation occurs, the revision becomes positive.
    //
    // Security: callers never own this value. UsersRegistry assigns and
    // persists it together with projection-relevant metadata.
    std::uint64_t storage_projection_revision = 0;

    // Placement intent is separate from the currently realized placement.
    //
    // ""                = no policy override
    // "business-backup" = stable Director-managed policy ID
    std::string placement_policy_id;
};

struct UserStorageMetadataUpdate {
    std::string storage_state;
    std::uint64_t quota_bytes = 0;
    std::string root_rel;
    std::string storage_pool_id;
    std::string storage_set_at;
    std::string storage_set_by;
};

class UsersRegistry {
public:
    using StorageProjectionCommitNotifier =
        std::function<void()>;

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // Best-effort latency hook for Director projection reconciliation.
    // Security: notification happens only after a successful durable
    // projection-relevant registry commit.
    void set_storage_projection_commit_notifier(
        StorageProjectionCommitNotifier notifier
    );

    // Lookups
    bool exists(const std::string& fp_hex) const;
    std::optional<UserRec> get(const std::string& fp_hex) const;

    // Policy helpers
    bool is_enabled_user(const std::string& fp_hex) const;   // status == enabled
    bool is_admin_enabled(const std::string& fp_hex) const;  // enabled + role==admin
    std::string role_of(const std::string& fp_hex) const;

    // Mutations (auditable by caller)
    bool ensure_present_disabled_user(const std::string& fp_hex, const std::string& now_iso);
    bool upsert(const UserRec& u);

    // Atomically persists a complete user-record replacement.
    //
    // If the Storage Node projection changes, UsersRegistry increments the
    // Director-owned projection revision before saving. Save failure restores
    // both the previous record and its previous revision.
    //
    // New records committed through this API start at revision 1.
    bool upsert_persisted(
        const UserRec& u,
        const std::string& path,
        bool* projection_changed,
        std::string* error
    );

    bool set_status(const std::string& fp_hex, const std::string& status);

    // Atomic write-through status mutation with projection revision handling.
    bool set_status_persisted(
        const std::string& fp_hex,
        const std::string& status,
        const std::string& path,
        bool* changed,
        std::string* error
    );
    // Atomically persists storage metadata and its Director-owned
    // projection revision.
    //
    // changed reports whether any persisted storage metadata changed.
    // projection_changed reports whether the Storage Node-visible projection
    // changed and therefore advanced storage_projection_revision.
    bool set_storage_metadata_persisted(
        const std::string& fp_hex,
        const UserStorageMetadataUpdate& update,
        const std::string& path,
        bool* changed,
        bool* projection_changed,
        std::string* error
    );

    // Atomic compare-and-switch used by storage migration.
    //
    // Only storage mapping fields are replaced:
    // - storage_pool_id
    // - root_rel
    // - storage_set_at
    // - storage_set_by
    //
    // Current storage_state and quota_bytes are deliberately preserved.
    //
    // expected_storage_pool_id uses the persisted representation:
    //     "" = default pool
    //
    // A stale expectation is a successful no-op:
    // return true, expectation_matched=false, changed=false.
    //
    // Security: pool comparison, mapping mutation, projection revision,
    // persistence and rollback all happen under one UsersRegistry lock.
    bool switch_storage_mapping_persisted_if_pool_current(
        const std::string& fp_hex,
        const std::string& expected_storage_pool_id,
        const std::string& new_storage_pool_id,
        const std::string& new_root_rel,
        const std::string& storage_set_at,
        const std::string& storage_set_by,
        const std::string& path,
        bool* expectation_matched,
        bool* changed,
        bool* projection_changed,
        std::string* error
    );

    bool set_role(const std::string& fp_hex, const std::string& role);
    bool set_name_notes(const std::string& fp_hex, const std::string& name, const std::string& notes);

    // Updates placement metadata in memory. The caller remains responsible for
    // validating the node against NodeRegistry and persisting users.json.
    bool set_home_node(
        const std::string& fp_hex,
        const std::string& home_node
    );

    // Atomically persists a placement change before committing it to memory.
    // A failed save preserves the previous in-memory placement.
    bool set_home_node_persisted(
        const std::string& fp_hex,
        const std::string& home_node,
        const std::string& path,
        bool* changed,
        std::string* error
    );

    // Compare-and-set placement persistence for preview/apply workflows.
    // A stale expectation is a successful no-op: return true with
    // expectation_matched=false and changed=false.
    bool set_home_node_persisted_if_current(
        const std::string& fp_hex,
        const std::string& expected_home_node,
        const std::string& expected_placement_policy_id,
        const std::string& new_home_node,
        const std::string& path,
        bool* changed,
        bool* expectation_matched,
        std::string* error
    );

    // Atomically clears an existing Director-managed placement.
    //
    // Security:
    // - expected_home_node must name the currently placed Storage Node;
    // - expected policy intent participates in the CAS but is never cleared;
    // - home_node clear and projection revision increment are persisted together;
    // - a stale expectation is a successful no-op.
    bool clear_home_node_persisted_if_current(
        const std::string& fp_hex,
        const std::string& expected_home_node,
        const std::string& expected_placement_policy_id,
        const std::string& path,
        bool* changed,
        bool* expectation_matched,
        std::string* error
    );

    // Atomically persists a policy-reference change before committing it to
    // memory. Empty policy ID deliberately clears the override.
    bool set_placement_policy_id_persisted(
        const std::string& fp_hex,
        const std::string& placement_policy_id,
        const std::string& path,
        bool* changed,
        std::string* error
    );

    bool touch_last_seen(const std::string& fp_hex, const std::string& now_iso);
    bool erase(const std::string& fp_hex);

    // Atomically delete an unplaced user from memory + users.json.
    //
    // Security:
    // - a user with home_node must first go through the cluster revoke flow;
    // - persistence failure restores the complete previous UserRec.
    //
    // placement_blocked=true is a successful policy no-op.
    bool erase_persisted_if_unplaced(
        const std::string& fp_hex,
        const std::string& path,
        bool* erased,
        bool* placement_blocked,
        std::string* error
    );

    // Snapshot for API
    std::unordered_map<std::string, UserRec> snapshot() const;

    // Aggregate-only cluster reference information. This does not expose
    // fingerprints or complete user records to node administration.
    std::unordered_map<
        std::string,
        std::size_t
    > home_node_assignment_counts() const;

    // Aggregate-only policy reference information. Empty policy overrides are
    // omitted, and no user fingerprints or complete records are exposed.
    std::unordered_map<
        std::string,
        std::size_t
    > placement_policy_assignment_counts() const;

private:
    // Caller must hold mu_. The noexcept copy helper ensures a notification
    // allocation failure can never turn an already-durable commit into failure.
    StorageProjectionCommitNotifier
    storage_projection_commit_notifier_copy_locked() const noexcept;

    // Caller must hold mu_.
    bool save_locked(
        const std::string& path
    ) const;

    mutable std::mutex mu_;
    std::unordered_map<std::string, UserRec> by_fp_;
    StorageProjectionCommitNotifier
        storage_projection_commit_notifier_;
};

} // namespace pqnas
