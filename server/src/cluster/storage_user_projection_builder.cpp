#include "cluster/storage_user_projection_builder.h"

#include "cluster/cluster_identity_validation.h"
#include "storage_metadata_validation.h"
#include "storage_path_validation.h"
#include "users_registry.h"

#include <string>

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

} // namespace

bool build_storage_user_projection(
    const pqnas::UserRec& user,
    StorageUserProjection* projection,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!projection) {
        set_error(
            error,
            "missing storage projection output"
        );
        return false;
    }

    // Security: only canonical Director user identities may cross the
    // Director -> Storage authorization boundary.
    if (!pqnas::valid_storage_user_fingerprint_hex(
            user.fingerprint)) {
        set_error(
            error,
            "invalid user fingerprint"
        );
        return false;
    }

    // A projection is meaningful only after Director placement has selected a
    // stable Storage Node. Standalone/local users must never be synchronized.
    if (!valid_cluster_node_id(
            user.home_node)) {
        set_error(
            error,
            "invalid or missing home node"
        );
        return false;
    }

    // Revision zero denotes legacy/unversioned Director state. Storage Nodes
    // reject it so delayed control-plane writes cannot bypass monotonic order.
    if (user.storage_projection_revision == 0) {
        set_error(
            error,
            "storage projection revision is not initialized"
        );
        return false;
    }

    if (user.storage_state != "allocated" &&
        user.storage_state != "unallocated") {
        set_error(
            error,
            "invalid storage state"
        );
        return false;
    }

    StorageUserProjection result;

    result.fingerprint =
        user.fingerprint;

    result.enabled =
        user.status == "enabled";

    result.storage_allocated =
        user.storage_state == "allocated";

    result.home_node =
        user.home_node;

    result.revision =
        user.storage_projection_revision;

    if (!result.storage_allocated) {
        // Security: never leak stale physical mapping/quota metadata from a
        // deallocated user record to Storage. Node-side schema requires this
        // canonical empty representation.
        result.quota_bytes = 0;
        result.root_rel.clear();
        result.storage_pool_id.clear();

        *projection =
            std::move(result);

        return true;
    }

    if (user.root_rel.empty() ||
        user.root_rel.size() >
            kStorageUserProjectionRootRelMaxBytes) {
        set_error(
            error,
            "invalid allocated storage root"
        );
        return false;
    }

    if (!pqnas::valid_storage_pool_reference(
            user.storage_pool_id)) {
        set_error(
            error,
            "invalid storage pool"
        );
        return false;
    }

    std::string normalized_root;
    std::string path_error;

    if (!pqnas::normalize_storage_relative_path_strict(
            user.root_rel,
            &normalized_root,
            &path_error) ||
        normalized_root != user.root_rel) {
        // Security: Director must send one canonical path. Never allow the
        // Storage Node to reinterpret traversal or normalization differences.
        set_error(
            error,
            "invalid or non-canonical storage root"
        );
        return false;
    }

    result.quota_bytes =
        user.quota_bytes;

    result.root_rel =
        user.root_rel;

    result.storage_pool_id =
        user.storage_pool_id;

    *projection =
        std::move(result);

    return true;
}

} // namespace pqnas::cluster
