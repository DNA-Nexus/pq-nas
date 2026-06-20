#pragma once

#include <httplib.h>

#include "routes_workspaces_files.h"

namespace pqnas {

void register_workspace_link_routes(httplib::Server& srv,
                                    const WorkspaceFileRouteDeps& deps);

} // namespace pqnas
