#pragma once

#include "cluster/storage_user_projection_audit_client.h"
#include "cluster/storage_user_projection_sync_client.h"

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

// RT-008 capability is injected rather than coupling the deterministic
// reconciler to NodeRegistry/protocol rollout. Returning a node here means
// that node must receive complete range proofs during this reconciliation.
using StorageUserProjectionReconcileAuditNodeList =
    std::function<
        std::vector<std::string>()
    >;

using StorageUserProjectionReconcileAuditInvoke =
    std::function<
        StorageUserProjectionAuditClientResult(
            const StorageUserProjectionAuditRequest&
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

// Deterministic RT-008 full-namespace partitioner.
//
// Expected identities are sorted lexically. Every request contains at most 512
// entries, start is inclusive, end exclusive, and zero expected identities
// still produce one "" -> "" request so restored orphan authorizations are
// observable.
bool build_storage_user_projection_reconcile_audit_requests(
    const std::string& node_id,
    std::vector<StorageUserProjectionAuditExpected> expected,
    std::vector<StorageUserProjectionAuditRequest>* requests
);

struct StorageUserProjectionReconcilerConfig {
    pqnas::UsersRegistry* users = nullptr;

    StorageUserProjectionSyncStateStore*
        state_store = nullptr;

    // Security: RT-009 durable write-ahead record for a Node/revision that
    // may accept authorization before confirmed state can be committed.
    StorageUserProjectionTransitionStore*
        transition_store = nullptr;

    // RT-016: shared with cluster-safe user/Node deletion in production.
    // The reconciler holds this only while re-reading authoritative Director
    // state and durably journaling a newly authorized Node. Network I/O must
    // never run while this mutex is held.
    //
    // Reusable single-threaded callers may omit it; production wiring is
    // structurally guarded to provide the shared boundary.
    std::shared_ptr<std::mutex>
        transition_mutex;

    StorageUserProjectionReconcileSyncInvoke
        sync_projection;

    // Optional RT-008 range-proof pair. If both are absent, equal confirmed
    // state is verified through the legacy idempotent projection POST. If one
    // is present, both are required and failures are fail-closed.
    StorageUserProjectionReconcileAuditNodeList
        list_audit_capable_nodes;

    StorageUserProjectionReconcileAuditInvoke
        audit_projection_range;

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
