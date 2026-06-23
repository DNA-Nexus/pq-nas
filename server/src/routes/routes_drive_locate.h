#pragma once

#include "httplib.h"

#include <functional>
#include <map>
#include <string>

namespace pqnas {

struct DriveLocateRoutesDeps {
    // Admin auth bridge injected from main.cpp.
    // Returns authenticated admin fingerprint in out_actor_fp when available.
    std::function<bool(const httplib::Request&,
                       httplib::Response&,
                       std::string*)> require_admin_actor;

    // Shared JSON response helper from main.cpp.
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;

    // Shared audit bridge from main.cpp.
    std::function<void(const std::string& event,
                       const std::string& outcome,
                       const std::map<std::string, std::string>& fields)> audit_emit;

    // Guarded root wrapper installed by the installer.
    std::string wrapper_path = "/usr/local/sbin/pqnas-drive-locate";
};

void register_drive_locate_routes(httplib::Server& srv,
                                  const DriveLocateRoutesDeps& deps);

} // namespace pqnas
