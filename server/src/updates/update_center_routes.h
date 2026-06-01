#pragma once

#include "httplib.h"

#include <functional>
#include <string>

namespace pqnas::updates {

struct UpdateCenterRoutesDeps {
    std::string static_admin_updates_html;

    std::function<bool(const std::string&, std::string&)> read_file_to_string;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_admin;
};

void register_update_center_routes(httplib::Server& srv, const UpdateCenterRoutesDeps& deps);

} // namespace pqnas::updates
