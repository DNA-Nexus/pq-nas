#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct CoreUiShellRoutesContext {
    bool is_director = false;
    bool is_storage_node = false;

    std::string static_system_js;
    std::string static_audit_html;
    std::string static_admin_html;
    std::string static_director_admin_html;
    std::string static_director_admin_css;
    std::string static_director_admin_js;
    std::string static_app_html;
    std::string static_app_js;
    std::string static_app_frame_background_tasks_js;
    std::string static_admin_js;

    std::string static_handoff_director_js;
    std::string static_handoff_storage_bootstrap_js;
    std::string static_handoff_storage_finish_js;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_user;
    std::function<bool(const httplib::Request&)> is_admin;
    std::function<bool(const std::string&, std::string&)> read_file_to_string;
    std::function<std::string(const std::string&)> slurp_file;
};

void handle_core_ui_admin(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& ctx
);

void handle_core_ui_app(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& ctx
);

void handle_core_ui_handoff_director(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& ctx
);

void handle_core_ui_handoff_storage_bootstrap(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& ctx
);

void handle_core_ui_handoff_storage_finish(
    const httplib::Request& req,
    httplib::Response& res,
    const CoreUiShellRoutesContext& ctx
);

void register_core_ui_shell_routes(
    httplib::Server& srv,
    const CoreUiShellRoutesContext& ctx
);
