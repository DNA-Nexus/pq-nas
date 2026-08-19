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

    // Security: authenticated identity alone is insufficient. Filesystem root
    // authorization must be re-resolved and may fail after revoke/relocation.
    std::function<bool(
        const std::string& fingerprint,
        std::filesystem::path* user_root
    )> resolve_user_root;
};

void register_file_version_archive_blob_routes(
    httplib::Server& srv,
    const FileVersionArchiveBlobRoutesContext& ctx
);
