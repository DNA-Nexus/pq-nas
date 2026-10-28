#include "cluster/storage_user_access_runtime.h"

#include <optional>
#include <utility>

namespace pqnas::cluster {
namespace {

StorageUserSessionPolicy::Config
make_session_policy_config(
    const StorageUserAccessRuntimeConfig& config
) {
    StorageUserSessionPolicy::Config
        policy_config;

    policy_config.local_node_id =
        config.local_node_id;

    const StorageUserProjectionStore*
        projections =
            config.projections;

    policy_config.lookup_projection =
        [projections](
            const std::string& fingerprint
        ) -> std::optional<StorageUserProjection> {
            if (!projections) {
                return std::nullopt;
            }

            // Security: always read the current projection. Never cache a
            // previous positive Storage authorization decision here.
            return projections->get(
                fingerprint
            );
        };

    return policy_config;
}

StorageUserSessionAuthenticatorConfig
make_session_authenticator_config(
    const StorageUserAccessRuntimeConfig& config,
    const StorageUserSessionPolicy* session_policy
) {
    StorageUserSessionAuthenticatorConfig
        authenticator_config;

    authenticator_config.session_policy =
        session_policy;

    authenticator_config.base_cookie_key =
        config.base_cookie_key;

    authenticator_config.local_node_id =
        config.local_node_id;

    authenticator_config.clock =
        config.clock;

    return authenticator_config;
}

StorageUserRootResolverConfig
make_root_resolver_config(
    const StorageUserAccessRuntimeConfig& config
) {
    StorageUserRootResolverConfig
        resolver_config;

    resolver_config.projections =
        config.projections;

    resolver_config.local_node_id =
        config.local_node_id;

    resolver_config.resolve_pool_data_root =
        config.resolve_pool_data_root;

    return resolver_config;
}

} // namespace

StorageUserAccessRuntime::StorageUserAccessRuntime(
    StorageUserAccessRuntimeConfig config
)
    : session_policy_(
          make_session_policy_config(
              config
          )
      ),
      session_authenticator_(
          make_session_authenticator_config(
              config,
              &session_policy_
          )
      ),
      root_resolver_(
          make_root_resolver_config(
              config
          )
      ) {
    const StorageUserSessionAuthenticator*
        authenticator =
            &session_authenticator_;

    request_auth_context_.authenticate =
        [authenticator](
            const std::string& cookie_value,
            StorageUserSessionAuthenticatorOutput* output
        ) {
            // Security: there is no Director-cookie or Bearer fallback at this
            // boundary; all requests use the Storage session authenticator.
            return authenticator->authenticate(
                cookie_value,
                output
            );
        };
}

const StorageUserSessionPolicy&
StorageUserAccessRuntime::session_policy() const noexcept {
    return session_policy_;
}

const StorageUserSessionAuthenticator&
StorageUserAccessRuntime::session_authenticator() const noexcept {
    return session_authenticator_;
}

const StorageUserRequestAuthContext&
StorageUserAccessRuntime::request_auth_context() const noexcept {
    return request_auth_context_;
}

const StorageUserRootResolver&
StorageUserAccessRuntime::root_resolver() const noexcept {
    return root_resolver_;
}

} // namespace pqnas::cluster
