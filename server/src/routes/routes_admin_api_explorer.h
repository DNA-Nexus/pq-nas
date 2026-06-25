#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct AdminApiExplorerRoutesContext {
    std::string html_path;
    std::string js_path;
    std::string catalog_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<std::string(const std::string&)> slurp_file;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
};

void register_admin_api_explorer_routes(
    httplib::Server& srv,
    const AdminApiExplorerRoutesContext& ctx
);
