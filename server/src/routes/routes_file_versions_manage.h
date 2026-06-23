#pragma once

#include <filesystem>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
class FileVersionsIndex;
class UsersRegistry;
}

struct FileVersionManageRoutesContext {
    pqnas::FileVersionsIndex* file_versions = nullptr;
    pqnas::UsersRegistry* users = nullptr;

    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)> require_user_auth;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<std::filesystem::path(const std::string&)> user_dir_for_fp;
};

void register_file_version_manage_routes(
    httplib::Server& srv,
    const FileVersionManageRoutesContext& ctx
);
