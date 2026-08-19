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
}

struct FileVersionArchiveBlobRoutesContext {
    pqnas::FileVersionsIndex* file_versions = nullptr;

    // Storage authentication returns the exact projection revision used for
    // its authorization decision. Director authentication leaves it as zero.
    std::function<bool(
        const httplib::Request&,
        httplib::Response&,
        std::string* fingerprint,
        std::string* role,
        std::uint64_t* authenticated_projection_revision
    )> require_user_auth;

    // Security: authenticated identity alone is insufficient. Filesystem root
    // authorization must be re-resolved and may fail after revoke/relocation.
    std::function<bool(
        const std::string& fingerprint,
        std::uint64_t authenticated_projection_revision,
        std::filesystem::path* user_root
    )> resolve_user_root;
};

void register_file_version_archive_blob_routes(
    httplib::Server& srv,
    const FileVersionArchiveBlobRoutesContext& ctx
);
