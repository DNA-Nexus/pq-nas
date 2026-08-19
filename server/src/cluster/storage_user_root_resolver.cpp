#include "cluster/storage_user_root_resolver.h"

#include "cluster/cluster_identity_validation.h"
#include "storage_metadata_validation.h"
#include "storage_path_validation.h"

#include <optional>
#include <utility>

namespace pqnas::cluster {
namespace {

void clear_output(
    StorageUserRootResolveOutput* output
) noexcept {
    if (!output) {
        return;
    }

    output->user_root.clear();
    output->projection_revision = 0;
    output->quota_bytes = 0;
    output->storage_pool_id.clear();
    output->root_rel.clear();
}

} // namespace

const char* storage_user_root_resolve_result_code(
    StorageUserRootResolveResult result
) noexcept {
    switch (result) {
        case StorageUserRootResolveResult::Resolved:
            return "resolved";

        case StorageUserRootResolveResult::InvalidInput:
            return "invalid_input";

        case StorageUserRootResolveResult::InvalidConfiguration:
            return "invalid_configuration";

        case StorageUserRootResolveResult::ProjectionMissing:
            return "projection_missing";

        case StorageUserRootResolveResult::Unauthorized:
            return "unauthorized";

        case StorageUserRootResolveResult::PoolUnavailable:
            return "pool_unavailable";

        case StorageUserRootResolveResult::InvalidPath:
            return "invalid_path";

        case StorageUserRootResolveResult::InternalError:
            return "internal_error";
    }

    return "internal_error";
}

StorageUserRootResolver::StorageUserRootResolver(
    StorageUserRootResolverConfig config
)
    : config_(std::move(config)) {
}

StorageUserRootResolveResult
StorageUserRootResolver::resolve(
    const std::string& fingerprint,
    StorageUserRootResolveOutput* output
) const noexcept {
    clear_output(output);

    if (!output ||
        !pqnas::valid_storage_user_fingerprint_hex(
            fingerprint)) {
        return StorageUserRootResolveResult::
            InvalidInput;
    }

    if (!config_.projections ||
        !valid_cluster_node_id(
            config_.local_node_id) ||
        !config_.resolve_pool_data_root) {
        return StorageUserRootResolveResult::
            InvalidConfiguration;
    }

    try {
        // Security: always fetch the current Node-local projection.
        // Never reuse a previously resolved filesystem root after a revoke,
        // move, disable or storage-allocation transition.
        const std::optional<StorageUserProjection>
            projection =
                config_.projections->get(
                    fingerprint
                );

        if (!projection.has_value()) {
            return StorageUserRootResolveResult::
                ProjectionMissing;
        }

        // Defense in depth: the persistent projection store already validates
        // these fields. Re-check them here because this component turns
        // authorization metadata into an absolute filesystem path.
        if (projection->fingerprint != fingerprint ||
            !pqnas::valid_storage_user_fingerprint_hex(
                projection->fingerprint) ||
            !valid_cluster_node_id(
                projection->home_node) ||
            projection->revision == 0 ||
            !pqnas::valid_storage_pool_reference(
                projection->storage_pool_id)) {
            return StorageUserRootResolveResult::
                Unauthorized;
        }

        if (!projection->enabled ||
            !projection->storage_allocated ||
            projection->home_node !=
                config_.local_node_id) {
            return StorageUserRootResolveResult::
                Unauthorized;
        }

        if (projection->root_rel.empty() ||
            projection->root_rel.size() >
                kStorageUserProjectionRootRelMaxBytes) {
            return StorageUserRootResolveResult::
                InvalidPath;
        }

        std::string normalized_root_rel;
        std::string path_error;

        if (!pqnas::
                normalize_storage_relative_path_strict(
                    projection->root_rel,
                    &normalized_root_rel,
                    &path_error
                ) ||
            normalized_root_rel !=
                projection->root_rel) {
            // Security: Director metadata must already be canonical.
            // Never reinterpret another spelling into a filesystem root.
            return StorageUserRootResolveResult::
                InvalidPath;
        }

        std::filesystem::path data_root;

        bool pool_resolved = false;

        try {
            pool_resolved =
                config_.resolve_pool_data_root(
                    projection->storage_pool_id,
                    &data_root
                );
        } catch (...) {
            // Security: a pool resolver failure must never degrade into
            // the default pool or another filesystem root.
            clear_output(output);

            return StorageUserRootResolveResult::
                InternalError;
        }

        if (!pool_resolved ||
            data_root.empty() ||
            !data_root.is_absolute()) {
            // Security: an unavailable named pool fails closed. In
            // particular, never substitute pqnas::data_root_dir().
            return StorageUserRootResolveResult::
                PoolUnavailable;
        }

        data_root =
            data_root.lexically_normal();

        if (data_root.empty() ||
            !data_root.is_absolute()) {
            return StorageUserRootResolveResult::
                PoolUnavailable;
        }

        const std::filesystem::path
            user_root =
                (
                    data_root /
                    std::filesystem::path(
                        projection->root_rel
                    )
                ).lexically_normal();

        if (user_root.empty() ||
            !user_root.is_absolute()) {
            return StorageUserRootResolveResult::
                InvalidPath;
        }

        output->user_root =
            user_root;

        output->projection_revision =
            projection->revision;

        output->quota_bytes =
            projection->quota_bytes;

        output->storage_pool_id =
            projection->storage_pool_id;

        output->root_rel =
            projection->root_rel;

        return StorageUserRootResolveResult::
            Resolved;
    } catch (...) {
        // Security: unexpected allocation/store/filesystem failures must
        // never leave a partially resolved path available to the caller.
        clear_output(output);

        return StorageUserRootResolveResult::
            InternalError;
    }
}

StorageUserRootResolveResult
StorageUserRootResolver::resolve_at_revision(
    const std::string& fingerprint,
    std::uint64_t expected_projection_revision,
    StorageUserRootResolveOutput* output
) const noexcept {
    clear_output(output);

    if (!output ||
        expected_projection_revision == 0) {
        return StorageUserRootResolveResult::
            InvalidInput;
    }

    // Security: resolve() performs exactly one fresh projection lookup and
    // returns both the filesystem root and the projection revision from that
    // same snapshot. Compare only after that resolution so root + revision
    // cannot come from two different projection reads.
    const StorageUserRootResolveResult result =
        resolve(
            fingerprint,
            output
        );

    if (result !=
        StorageUserRootResolveResult::
            Resolved) {
        return result;
    }

    if (output->projection_revision !=
        expected_projection_revision) {
        // Security: never expose a filesystem root authorized by a different
        // projection revision than the one authenticated for this request.
        clear_output(output);

        return StorageUserRootResolveResult::
            Unauthorized;
    }

    return StorageUserRootResolveResult::
        Resolved;
}

} // namespace pqnas::cluster
