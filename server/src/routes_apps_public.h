#pragma once

#include <functional>
#include <string>

#include <nlohmann/json.hpp>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct AppsPublicRoutesContext {
    std::string apps_installed_dir;
    std::string apps_bundled_dir;
    std::string server_version;

    std::function<nlohmann::json()> load_app_launch_policy_json;
    std::function<bool(const httplib::Request&)> is_admin_cookie;
    std::function<void(const std::string&, const std::string&, const std::string&, httplib::Response&, bool)> serve_file_under_root;
    std::function<bool(const std::string&, std::string&)> read_file_to_string;
    std::function<std::string(const std::string&)> rel_to_repo;
};

void register_apps_public_routes(
    httplib::Server& srv,
    const AppsPublicRoutesContext& ctx
);
