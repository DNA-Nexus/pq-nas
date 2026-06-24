#pragma once

#include <httplib.h>

#include "routes_workspaces_files.h"

namespace pqnas {

// External workspace message-board routes.
//
// These routes intentionally reuse WorkspaceFileRouteDeps so the external
// message board follows the same auth, cookie, users/workspaces registry,
// audit and now_epoch wiring as workspace file routes.
void register_workspace_external_message_routes(
    httplib::Server& srv,
    const WorkspaceFileRouteDeps& deps);

} // namespace pqnas
