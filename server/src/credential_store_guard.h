#pragma once

#include <cerrno>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace pqnas {

inline std::string credential_store_normalized_path(
    const std::string& path
) {
    if (path.empty()) {
        return {};
    }

    std::error_code ec;
    const std::filesystem::path absolute =
        std::filesystem::absolute(path, ec);

    if (ec) {
        return {};
    }

    return absolute.lexically_normal().string();
}

inline bool credential_store_generation_token(
    const std::string& path,
    std::string* out
) {
    if (!out || path.empty()) {
        return false;
    }

    out->clear();

    struct stat st {};
    if (::lstat(path.c_str(), &st) != 0) {
        if (errno == ENOENT) {
            *out = "missing";
            return true;
        }

        return false;
    }

    // Security: credential stores must be ordinary files. Refuse symlinks,
    // devices and other special files before authentication state is read.
    if (!S_ISREG(st.st_mode)) {
        return false;
    }

    *out =
        "present:" +
        std::to_string(
            static_cast<unsigned long long>(st.st_dev)
        ) + ":" +
        std::to_string(
            static_cast<unsigned long long>(st.st_ino)
        ) + ":" +
        std::to_string(
            static_cast<long long>(st.st_size)
        ) + ":" +
        std::to_string(
            static_cast<long long>(st.st_mtim.tv_sec)
        ) + ":" +
        std::to_string(
            static_cast<long long>(st.st_mtim.tv_nsec)
        ) + ":" +
        std::to_string(
            static_cast<long long>(st.st_ctim.tv_sec)
        ) + ":" +
        std::to_string(
            static_cast<long long>(st.st_ctim.tv_nsec)
        );

    return true;
}

class CredentialStoreFileLock {
public:
    enum class Mode {
        shared,
        exclusive
    };

    CredentialStoreFileLock(
        const std::string& store_path,
        Mode mode
    ) {
        if (store_path.empty()) {
            return;
        }

        const std::filesystem::path target(store_path);
        const std::filesystem::path parent =
            target.parent_path();

        std::error_code ec;
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                return;
            }
        }

        const std::filesystem::path lock_path =
            target.string() + ".lock";

        int flags = O_CREAT | O_RDWR;

#ifdef O_CLOEXEC
        flags |= O_CLOEXEC;
#endif

#ifdef O_NOFOLLOW
        flags |= O_NOFOLLOW;
#endif

        fd_ = ::open(lock_path.c_str(), flags, 0600);
        if (fd_ < 0) {
            return;
        }

        const int operation =
            mode == Mode::shared
                ? LOCK_SH
                : LOCK_EX;

        if (::flock(fd_, operation) != 0) {
            (void)::close(fd_);
            fd_ = -1;
            return;
        }

        ok_ = true;
    }

    CredentialStoreFileLock(
        const CredentialStoreFileLock&
    ) = delete;

    CredentialStoreFileLock& operator=(
        const CredentialStoreFileLock&
    ) = delete;

    ~CredentialStoreFileLock() {
        if (fd_ >= 0) {
            (void)::flock(fd_, LOCK_UN);
            (void)::close(fd_);
        }
    }

    bool ok() const {
        return ok_;
    }

private:
    int fd_ = -1;
    bool ok_ = false;
};

} // namespace pqnas
