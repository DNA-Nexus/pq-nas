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
}

struct FileVersionArchiveBlobRoutesContext {
    pqnas::FileVersionsIndex* file_versions = nullptr;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)> require_user_auth;
    std::function<std::filesystem::path(const std::string&)> user_dir_for_fp;
};

void register_file_version_archive_blob_routes(
    httplib::Server& srv,
    const FileVersionArchiveBlobRoutesContext& ctx
);
