#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
class UsersRegistry;
class WorkspacesRegistry;
struct AuditEvent;
}

struct UserAvatarRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    pqnas::WorkspacesRegistry* workspaces = nullptr;
    std::string users_path;
    std::string workspaces_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin_cookie;
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)> require_user_cookie;
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)> require_user_auth;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;

    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<bool(const std::string&, std::string&)> b64std_decode_to_bytes;
    std::function<bool(const std::string&)> is_valid_fingerprint_hex;
    std::function<void(const pqnas::AuditEvent&)> audit_append;
};

void register_user_avatar_routes(
    httplib::Server& srv,
    const UserAvatarRoutesContext& ctx
);
