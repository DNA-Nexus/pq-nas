#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
class UsersRegistry;
}

struct AdminUsersOverviewRoutesContext {
    pqnas::UsersRegistry* users = nullptr;
    std::string users_path;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)> require_admin_auth;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<std::filesystem::path(const std::string&)> user_dir_for_fp;
    std::function<std::uint64_t(const std::filesystem::path&)> dir_size_bytes_best_effort;
};

void register_admin_users_overview_routes(
    httplib::Server& srv,
    const AdminUsersOverviewRoutesContext& ctx
);
