#pragma once

#include <httplib.h>

namespace pqnas {

// Security boundary for normal local user authentication.
//
// Storage Nodes must not create normal user sessions or credentials locally.
// Their normal user session entry point is the authenticated Director handoff.
//
// Returns true when the caller may continue.
// Returns false after writing the fail-closed HTTP response.
bool require_interactive_user_login(
    bool interactive_user_login_allowed,
    httplib::Response& response
);

} // namespace pqnas
