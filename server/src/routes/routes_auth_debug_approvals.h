#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

struct AuthDebugApprovalsRoutesContext {
    std::function<bool()> auth_debug_enabled;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_admin_cookie;
    std::function<int()> approvals_count;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
};

void register_auth_debug_approvals_routes(
    httplib::Server& srv,
    const AuthDebugApprovalsRoutesContext& ctx
);
