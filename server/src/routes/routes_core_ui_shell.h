#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct CoreUiShellRoutesContext {
    std::string static_system_js;
    std::string static_audit_html;
    std::string static_admin_html;
    std::string static_app_js;
    std::string static_app_frame_background_tasks_js;
    std::string static_admin_js;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<bool(const std::string&, std::string&)> read_file_to_string;
    std::function<std::string(const std::string&)> slurp_file;
};

void register_core_ui_shell_routes(
    httplib::Server& srv,
    const CoreUiShellRoutesContext& ctx
);
