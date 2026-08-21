#include "durable_file.h"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <unistd.h>

namespace pqnas {
namespace {

void set_errno_error(
    std::string* error,
    const std::string& prefix
) {
    if (!error) return;

    *error =
        prefix +
        ": " +
        std::strerror(errno);
}

} // namespace

bool fsync_file_and_parent_directory(
    const std::string& path,
    std::string* error
) {
    if (error) error->clear();

    if (path.empty()) {
        if (error) *error = "empty path";
        return false;
    }

    const std::filesystem::path target(path);

    std::filesystem::path parent =
        target.parent_path();

    if (parent.empty()) {
        parent = ".";
    }

    const std::string filename =
        target.filename().string();

    if (filename.empty() ||
        filename == "." ||
        filename == "..") {
        if (error) *error = "invalid target filename";
        return false;
    }

    const int directory_fd =
        ::open(
            parent.c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC
        );

    if (directory_fd < 0) {
        set_errno_error(
            error,
            "open parent directory failed"
        );
        return false;
    }

    const int file_fd =
        ::openat(
            directory_fd,
            filename.c_str(),
            O_RDONLY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (file_fd < 0) {
        set_errno_error(
            error,
            "open committed file failed"
        );
        (void)::close(directory_fd);
        return false;
    }

    // Security: persist the renamed inode contents before committing its
    // directory entry as durable.
    if (::fsync(file_fd) != 0) {
        set_errno_error(
            error,
            "file fsync failed"
        );
        (void)::close(file_fd);
        (void)::close(directory_fd);
        return false;
    }

    if (::close(file_fd) != 0) {
        set_errno_error(
            error,
            "close committed file failed"
        );
        (void)::close(directory_fd);
        return false;
    }

    // Security: fsync the parent directory so the rename itself survives
    // a crash or sudden power loss.
    if (::fsync(directory_fd) != 0) {
        set_errno_error(
            error,
            "parent directory fsync failed"
        );
        (void)::close(directory_fd);
        return false;
    }

    (void)::close(directory_fd);
    return true;
}

} // namespace pqnas
