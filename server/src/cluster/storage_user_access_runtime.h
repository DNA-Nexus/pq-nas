#pragma once

#include "cluster/storage_user_request_auth.h"
#include "cluster/storage_user_root_resolver.h"
#include "cluster/storage_user_session_authenticator.h"
#include "cluster/storage_user_session_policy.h"

#include <string>

namespace pqnas::cluster {

struct StorageUserAccessRuntimeConfig {
    // Must outlive this runtime.
    const StorageUserProjectionStore*
        projections = nullptr;

    // Must point to exactly 32 bytes and outlive this runtime.
    const unsigned char*
        base_cookie_key = nullptr;

    std::string local_node_id;

    // Security: this must be the strict Node-local pool resolver. Unknown
    // named pools must fail rather than falling back to the default pool.
    StoragePoolDataRootResolver
        resolve_pool_data_root;

    StorageUserSessionAuthenticatorClock
        clock;
};

// Owns the complete Storage user authorization composition.
//
// Security:
// - session policy reads the current Node-local projection on every request
// - session authenticator requires exact projection revision equality
// - request auth accepts only the Storage-domain session cookie
// - filesystem roots are resolved from fresh projections + strict pool mapping
// - no UsersRegistry or Director session fallback exists here
class StorageUserAccessRuntime {
public:
    explicit StorageUserAccessRuntime(
        StorageUserAccessRuntimeConfig config
    );

    StorageUserAccessRuntime(
        const StorageUserAccessRuntime&
    ) = delete;

    StorageUserAccessRuntime&
    operator=(
        const StorageUserAccessRuntime&
    ) = delete;

    // Security: the request-auth callback stores an internal component pointer.
    // Keep the runtime at a stable address for its complete lifetime.
    StorageUserAccessRuntime(
        StorageUserAccessRuntime&&
    ) = delete;

    StorageUserAccessRuntime&
    operator=(
        StorageUserAccessRuntime&&
    ) = delete;

    const StorageUserSessionPolicy&
    session_policy() const noexcept;

    const StorageUserSessionAuthenticator&
    session_authenticator() const noexcept;

    const StorageUserRequestAuthContext&
    request_auth_context() const noexcept;

    const StorageUserRootResolver&
    root_resolver() const noexcept;

private:
    StorageUserSessionPolicy
        session_policy_;

    StorageUserSessionAuthenticator
        session_authenticator_;

    StorageUserRootResolver
        root_resolver_;

    StorageUserRequestAuthContext
        request_auth_context_;
};

} // namespace pqnas::cluster
