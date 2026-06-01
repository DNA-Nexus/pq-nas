#pragma once

#include "httplib.h"

namespace pqnas::updates {

void register_update_center_routes(httplib::Server& srv);

} // namespace pqnas::updates
