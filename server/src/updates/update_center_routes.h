#pragma once

#include "httplib.h"

#include <functional>
#include <map>
#include <string>

namespace pqnas::updates {

using UpdateCenterAuditEmitFn =
    std::function<void(const std::string& event,
                       const std::string& outcome,
                       const std::map<std::string, std::string>& fields)>;

struct UpdateCenterRoutesDeps {
    std::string static_admin_updates_html;
    std::string apps_installed_dir;

    std::function<bool(const std::string&, std::string&)> read_file_to_string;
    std::function<std::string()> static_root_dir;
    std::function<std::string()> config_root_dir;
    std::function<std::string(const char*)> getenv_str;
    std::function<std::string(const std::string&)> sha256_hex;

    std::function<bool(const httplib::Request&, httplib::Response&)> require_admin;
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin_actor;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;

    UpdateCenterAuditEmitFn audit_emit;
};

void register_update_center_routes(httplib::Server& srv, const UpdateCenterRoutesDeps& deps);

} // namespace pqnas::updates
