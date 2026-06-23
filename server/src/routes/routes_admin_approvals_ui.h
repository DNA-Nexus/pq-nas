#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct AdminApprovalsUiRoutesContext {
    std::string approvals_html_path;
    std::string approvals_js_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin;
    std::function<std::string(const std::string&)> slurp_file;
};

void register_admin_approvals_ui_routes(
    httplib::Server& srv,
    const AdminApprovalsUiRoutesContext& ctx
);
