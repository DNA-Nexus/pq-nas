#pragma once

#include "cluster/storage_user_projection_sync_state_store.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pqnas::cluster {

// Security: RT-009 pending transition knowledge.
//
// A record means this Node/revision may receive or may already have received
// authorization, but is not yet safely represented as the confirmed target.
//
// Storage mechanics deliberately reuse the hardened sync-state persistence
// primitive: bounded parsing, private-file checks, atomic rename, fsync and
// durability-uncertainty healing.
using StorageUserProjectionTransitionRecord =
    StorageUserProjectionSyncState;

class StorageUserProjectionTransitionStore {
public:
    bool load(
        const std::string& path,
        std::string* error
    ) {
        return store_.load(
            path,
            error
        );
    }

    std::optional<
        StorageUserProjectionTransitionRecord
    >
    get(
        const std::string& fingerprint
    ) const {
        return store_.get(
            fingerprint
        );
    }

    std::vector<
        StorageUserProjectionTransitionRecord
    >
    snapshot() const {
        return store_.snapshot();
    }

    bool ensure_durable(
        std::string* error
    ) {
        return store_.ensure_durable(
            error
        );
    }

    StorageUserProjectionSyncStateStoreResult
    mark_pending(
        const StorageUserProjectionTransitionRecord& record,
        std::string* error
    ) {
        return store_.mark_synced(
            record,
            error
        );
    }

    bool erase_if_current(
        const std::string& fingerprint,
        const std::string& expected_home_node,
        std::uint64_t expected_revision,
        bool* expectation_matched,
        std::string* error
    ) {
        return store_.erase_if_current(
            fingerprint,
            expected_home_node,
            expected_revision,
            expectation_matched,
            error
        );
    }

private:
    StorageUserProjectionSyncStateStore
        store_;
};

} // namespace pqnas::cluster
