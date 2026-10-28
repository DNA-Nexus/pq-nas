#pragma once

#include "cluster/storage_user_session_authenticator.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace httplib {
struct Request;
struct Response;
}

namespace pqnas::cluster {

// Security: cap attacker-controlled Cookie parsing before cryptographic work.
inline constexpr std::size_t
    kStorageUserRequestMaxCookieHeaderBytes = 4096;

inline constexpr const char*
    kStorageUserRequestSessionCookieName =
        "pqnas_storage_session";

using StorageUserRequestAuthenticate =
    std::function<
        StorageUserSessionAuthenticatorResult(
            const std::string& cookie_value,
            StorageUserSessionAuthenticatorOutput* output
        )
    >;

struct StorageUserRequestAuthContext {
    StorageUserRequestAuthenticate authenticate;
};

// Browser/API request boundary for Storage Node user operations.
//
// Security:
// - accepts exactly one Cookie header
// - accepts exactly one pqnas_storage_session cookie
// - never falls back to Director pqnas_session
// - never accepts Bearer auth
// - delegates cryptographic verification + fresh projection authorization
//   to StorageUserSessionAuthenticator
// - dynamic authentication failures intentionally remain generic
bool require_storage_user_request_auth(
    const httplib::Request& request,
    httplib::Response& response,
    const StorageUserRequestAuthContext& context,
    std::string* out_fingerprint,
    std::string* out_role,
    std::uint64_t* out_projection_revision = nullptr
);

} // namespace pqnas::cluster
