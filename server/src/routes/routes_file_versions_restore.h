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
class FileVersionsIndex;
class UsersRegistry;
}

struct FileVersionRestoreRoutesContext {
    pqnas::FileVersionsIndex* file_versions = nullptr;
    pqnas::UsersRegistry* users = nullptr;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)> require_user_auth;
    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<std::filesystem::path(const std::string&)> user_dir_for_fp;

    std::function<std::uint64_t(const std::filesystem::path&)> file_size_u64;
    std::function<std::int64_t(const std::filesystem::path&)> file_mtime_epoch;

    std::function<void(const std::string& scope_type,
                       const std::string& scope_id,
                       const std::string& logical_rel_path,
                       std::uint64_t size_bytes,
                       std::int64_t mtime_epoch,
                       std::int64_t now_epoch)> touch_gallery_file_facts;
};

void register_file_version_restore_routes(
    httplib::Server& srv,
    const FileVersionRestoreRoutesContext& ctx
);
