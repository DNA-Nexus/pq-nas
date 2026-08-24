#pragma once

#include "cluster/storage_user_projection_sync_client.h"

#include <cstddef>
#include <functional>
#include <map>
#include <string>

namespace pqnas {

class UsersRegistry;

} // namespace pqnas

namespace pqnas::cluster {

class StorageUserProjectionSyncStateStore;
class StorageUserProjectionTransitionStore;

using StorageUserProjectionReconcileSyncInvoke =
    std::function<
        StorageUserProjectionSyncResult(
            const StorageUserProjection&
        )
    >;

using StorageUserProjectionReconcileAuditEmit =
    std::function<
        void(
            const std::string& event,
            const std::string& outcome,
            const std::map<
                std::string,
                std::string
            >& fields
        )
    >;

struct StorageUserProjectionReconcileResult {
    bool ok = false;

    std::size_t users_seen = 0;
    std::size_t no_placement = 0;
    std::size_t already_synced = 0;

    std::size_t synced_current = 0;
    std::size_t moved = 0;
    std::size_t revoked_unplaced = 0;

    std::size_t orphaned_states = 0;
    std::size_t stale_states = 0;
    std::size_t state_conflicts = 0;
    std::size_t persist_failures = 0;
    std::size_t failed = 0;
};

struct StorageUserProjectionReconcilerConfig {
    pqnas::UsersRegistry* users = nullptr;

    StorageUserProjectionSyncStateStore*
        state_store = nullptr;

    // Security: RT-009 durable write-ahead record for a Node/revision that
    // may accept authorization before confirmed state can be committed.
    StorageUserProjectionTransitionStore*
        transition_store = nullptr;

    StorageUserProjectionReconcileSyncInvoke
        sync_projection;

    // Best-effort audit sink. Reconciliation safety never depends on audit I/O.
    StorageUserProjectionReconcileAuditEmit
        audit_emit;
};

class StorageUserProjectionReconciler {
public:
    explicit StorageUserProjectionReconciler(
        StorageUserProjectionReconcilerConfig config
    );

    // Deterministic one-shot reconciliation.
    //
    // No worker thread, sleeps or retries are hidden here. This makes the
    // authorization transition policy directly testable.
    StorageUserProjectionReconcileResult
    reconcile_once() const;

private:
    StorageUserProjectionReconcilerConfig config_;
};

} // namespace pqnas::cluster
