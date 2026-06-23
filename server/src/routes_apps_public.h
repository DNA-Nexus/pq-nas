#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

#include <nlohmann/json.hpp>

struct AppsPublicRoutesContext {
    std::string apps_installed_dir;

    std::function<nlohmann::json()> load_app_launch_policy_json;
    std::function<bool(const httplib::Request&)> is_admin_cookie;
    std::function<void(const std::string&, const std::string&, const std::string&, httplib::Response&, bool)> serve_file_under_root;
};

void register_apps_public_routes(
    httplib::Server& srv,
    const AppsPublicRoutesContext& ctx
);
