#include "cluster/node_credential_registry.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"


#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

constexpr std::size_t kMaximumRegistryPathBytes = 4096;
constexpr std::size_t kMaximumCredentialFiles = 10000;
constexpr std::size_t kMaximumCredentialFileBytes =
    kInternalAuthTokenChars + 1;
constexpr char kCredentialSuffix[] = ".credential";
constexpr std::size_t kCredentialSuffixChars =
    sizeof(kCredentialSuffix) - 1;

std::atomic<std::uint64_t> g_temp_counter{0};

class UniqueFd {
public:
    explicit UniqueFd(int descriptor = -1) noexcept
        : descriptor_(descriptor) {}

    ~UniqueFd() {
        if (descriptor_ >= 0) {
            ::close(descriptor_);
        }
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : descriptor_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            if (descriptor_ >= 0) {
                ::close(descriptor_);
            }

            descriptor_ = other.release();
        }

        return *this;
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        const int result = descriptor_;
        descriptor_ = -1;
        return result;
    }

private:
    int descriptor_;
};

class UniqueDirectoryStream {
public:
    explicit UniqueDirectoryStream(DIR* stream = nullptr) noexcept
        : stream_(stream) {}

    ~UniqueDirectoryStream() {
        if (stream_) {
            ::closedir(stream_);
        }
    }

    UniqueDirectoryStream(const UniqueDirectoryStream&) = delete;
    UniqueDirectoryStream& operator=(const UniqueDirectoryStream&) = delete;

    DIR* get() const noexcept {
        return stream_;
    }

private:
    DIR* stream_;
};

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(std::string& value) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_internal_auth_token_text(&value_);
    }

    SensitiveStringWiper(const SensitiveStringWiper&) = delete;
    SensitiveStringWiper& operator=(const SensitiveStringWiper&) = delete;

private:
    std::string& value_;
};

enum class DirectoryOpenResult {
    Ok,
    Missing,
    Unsafe,
    IoFailed
};

enum class CredentialFileResult {
    Ok,
    Missing,
    Unsafe,
    Invalid,
    IoFailed
};

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

bool normalize_directory_path(
    const std::string& raw,
    std::filesystem::path* normalized,
    std::string* error
) {
    if (!normalized) {
        set_error(
            error,
            "missing node credential directory output"
        );
        return false;
    }

    if (raw.empty() ||
        raw.size() > kMaximumRegistryPathBytes) {
        set_error(
            error,
            "invalid node credential directory path"
        );
        return false;
    }

    const std::filesystem::path path(raw);

    if (!path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            error,
            "node credential directory path must be absolute"
        );
        return false;
    }

    for (const auto& component : path) {
        const std::string part = component.string();

        if (part == "." || part == "..") {
            set_error(
                error,
                "node credential directory path contains traversal"
            );
            return false;
        }
    }

    const std::filesystem::path clean =
        path.lexically_normal();

    if (clean.empty() ||
        clean == clean.root_path() ||
        clean.filename().empty()) {
        set_error(
            error,
            "node credential directory path must name a directory"
        );
        return false;
    }

    *normalized = clean;
    return true;
}

bool validate_directory_descriptor(
    int descriptor,
    std::string* error
) {
    struct stat stats {};

    if (::fstat(descriptor, &stats) != 0) {
        set_error(
            error,
            "failed to inspect node credential directory"
        );
        return false;
    }

    const uid_t effective_uid = ::geteuid();

    // Security: the per-node credential directory contains reusable plaintext
    // authentication material and therefore must be service-private.
    if (!S_ISDIR(stats.st_mode) ||
        (stats.st_mode & 0077) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe node credential directory"
        );
        return false;
    }

    return true;
}

DirectoryOpenResult open_registry_directory(
    const std::filesystem::path& path,
    bool create_if_missing,
    UniqueFd* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing node credential directory descriptor"
        );
        return DirectoryOpenResult::IoFailed;
    }

    bool created = false;

    if (create_if_missing) {
        std::error_code filesystem_error;
        created = std::filesystem::create_directories(
            path,
            filesystem_error
        );

        if (filesystem_error) {
            set_error(
                error,
                "failed to create node credential directory"
            );
            return DirectoryOpenResult::IoFailed;
        }
    }

    const int raw_descriptor = ::open(
        path.c_str(),
        O_RDONLY |
        O_DIRECTORY |
        O_CLOEXEC |
        O_NOFOLLOW
    );

    if (raw_descriptor < 0) {
        if (errno == ENOENT && !create_if_missing) {
            return DirectoryOpenResult::Missing;
        }

        if (errno == ELOOP || errno == ENOTDIR) {
            set_error(
                error,
                "unsafe node credential directory"
            );
            return DirectoryOpenResult::Unsafe;
        }

        set_error(
            error,
            "failed to open node credential directory"
        );
        return DirectoryOpenResult::IoFailed;
    }

    UniqueFd descriptor(raw_descriptor);

    if (created) {
        struct stat created_stats {};

        // Security: change permissions only on the opened inode and only when
        // this process created and owns the dedicated credential directory.
        if (::fstat(
                descriptor.get(),
                &created_stats) != 0 ||
            !S_ISDIR(created_stats.st_mode) ||
            created_stats.st_uid != ::geteuid()) {
            set_error(
                error,
                "unsafe newly created node credential directory"
            );
            return DirectoryOpenResult::Unsafe;
        }

        if (::fchmod(
                descriptor.get(),
                0700) != 0) {
            set_error(
                error,
                "failed to secure node credential directory"
            );
            return DirectoryOpenResult::IoFailed;
        }
    }

    if (!validate_directory_descriptor(
            descriptor.get(),
            error)) {
        return DirectoryOpenResult::Unsafe;
    }

    *output = std::move(descriptor);
    return DirectoryOpenResult::Ok;
}

std::string credential_filename(
    const std::string& node_id
) {
    return node_id + kCredentialSuffix;
}

bool decimal_component(
    const std::string& value
) {
    if (value.empty() || value.size() > 20) {
        return false;
    }

    for (unsigned char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }
    }

    return true;
}

bool parse_temporary_credential_filename(
    const std::string& filename
) {
    constexpr char marker[] = ".credential.tmp.";
    constexpr std::size_t marker_chars =
        sizeof(marker) - 1;

    if (filename.size() <= marker_chars + 3 ||
        filename.front() != '.') {
        return false;
    }

    const std::size_t marker_position =
        filename.rfind(marker);

    if (marker_position == std::string::npos ||
        marker_position <= 1) {
        return false;
    }

    const std::string node_id = filename.substr(
        1,
        marker_position - 1
    );

    if (!valid_cluster_node_id(node_id)) {
        return false;
    }

    const std::size_t suffix_offset =
        marker_position + marker_chars;

    const std::size_t separator = filename.find(
        '.',
        suffix_offset
    );

    if (separator == std::string::npos ||
        filename.find('.', separator + 1) !=
            std::string::npos) {
        return false;
    }

    return decimal_component(
        filename.substr(
            suffix_offset,
            separator - suffix_offset
        )
    ) && decimal_component(
        filename.substr(separator + 1)
    );
}

bool parse_credential_filename(
    const std::string& filename,
    std::string* node_id
) {
    if (!node_id ||
        filename.size() <= kCredentialSuffixChars ||
        filename.compare(
            filename.size() - kCredentialSuffixChars,
            kCredentialSuffixChars,
            kCredentialSuffix) != 0) {
        return false;
    }

    const std::string candidate = filename.substr(
        0,
        filename.size() - kCredentialSuffixChars
    );

    if (!valid_cluster_node_id(candidate)) {
        return false;
    }

    *node_id = candidate;
    return true;
}

bool safe_credential_metadata(
    const struct stat& stats
) {
    const uid_t effective_uid = ::geteuid();

    return
        S_ISREG(stats.st_mode) &&
        stats.st_nlink == 1 &&
        (stats.st_mode & 0077) == 0 &&
        (stats.st_uid == effective_uid ||
         stats.st_uid == 0);
}

CredentialFileResult inspect_existing_target(
    int directory_descriptor,
    const std::string& filename,
    bool* exists,
    std::string* error
) {
    if (exists) {
        *exists = false;
    }

    struct stat stats {};

    if (::fstatat(
            directory_descriptor,
            filename.c_str(),
            &stats,
            AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return CredentialFileResult::Missing;
        }

        set_error(
            error,
            "failed to inspect node credential target"
        );
        return CredentialFileResult::IoFailed;
    }

    if (!safe_credential_metadata(stats)) {
        set_error(
            error,
            "unsafe node credential target"
        );
        return CredentialFileResult::Unsafe;
    }

    if (exists) {
        *exists = true;
    }

    return CredentialFileResult::Ok;
}

bool read_all_bounded(
    int descriptor,
    std::string* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing node credential token buffer"
        );
        return false;
    }

    clear_internal_auth_token_text(output);

    try {
        // Security: read directly into the wipeable string. A separate stack
        // buffer would retain a second plaintext credential copy after return.
        output->assign(
            kMaximumCredentialFileBytes + 1,
            '\0'
        );
    } catch (...) {
        set_error(
            error,
            "failed to allocate node credential buffer"
        );
        return false;
    }

    std::size_t total = 0;

    while (total < output->size()) {
        const ssize_t count = ::read(
            descriptor,
            output->data() + total,
            output->size() - total
        );

        if (count > 0) {
            total += static_cast<std::size_t>(count);
            continue;
        }

        if (count == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        clear_internal_auth_token_text(output);
        set_error(
            error,
            "failed to read node credential"
        );
        return false;
    }

    if (total > kMaximumCredentialFileBytes) {
        clear_internal_auth_token_text(output);
        set_error(
            error,
            "node credential file is too large"
        );
        return false;
    }

    output->resize(total);
    return true;
}

CredentialFileResult read_credential_token(
    int directory_descriptor,
    const std::string& filename,
    std::string* token,
    std::string* error
) {
    if (!token) {
        set_error(
            error,
            "missing node credential token output"
        );
        return CredentialFileResult::IoFailed;
    }

    clear_internal_auth_token_text(token);

    const int raw_descriptor = ::openat(
        directory_descriptor,
        filename.c_str(),
        O_RDONLY |
        O_CLOEXEC |
        O_NOFOLLOW
    );

    if (raw_descriptor < 0) {
        if (errno == ENOENT) {
            return CredentialFileResult::Missing;
        }

        if (errno == ELOOP) {
            set_error(
                error,
                "unsafe node credential target"
            );
            return CredentialFileResult::Unsafe;
        }

        set_error(
            error,
            "failed to open node credential"
        );
        return CredentialFileResult::IoFailed;
    }

    UniqueFd descriptor(raw_descriptor);
    struct stat stats {};

    if (::fstat(
            descriptor.get(),
            &stats) != 0) {
        set_error(
            error,
            "failed to inspect opened node credential"
        );
        return CredentialFileResult::IoFailed;
    }

    if (!safe_credential_metadata(stats)) {
        set_error(
            error,
            "unsafe node credential target"
        );
        return CredentialFileResult::Unsafe;
    }

    if (stats.st_size < 0 ||
        static_cast<unsigned long long>(stats.st_size) <
            static_cast<unsigned long long>(
                kInternalAuthTokenChars) ||
        static_cast<unsigned long long>(stats.st_size) >
            static_cast<unsigned long long>(
                kMaximumCredentialFileBytes)) {
        set_error(
            error,
            "invalid node credential file size"
        );
        return CredentialFileResult::Invalid;
    }

    if (!read_all_bounded(
            descriptor.get(),
            token,
            error)) {
        return CredentialFileResult::IoFailed;
    }

    if (!token->empty() &&
        token->back() == '\n') {
        token->pop_back();
    }

    if (token->find('\n') != std::string::npos ||
        token->find('\r') != std::string::npos ||
        !valid_internal_auth_credential_token(*token)) {
        clear_internal_auth_token_text(token);
        set_error(
            error,
            "invalid node credential token"
        );
        return CredentialFileResult::Invalid;
    }

    return CredentialFileResult::Ok;
}

bool validate_registry_entries(
    int directory_descriptor,
    std::string* error
) {
    const int scan_descriptor = ::fcntl(
        directory_descriptor,
        F_DUPFD_CLOEXEC,
        0
    );

    if (scan_descriptor < 0) {
        set_error(
            error,
            "failed to duplicate node credential directory"
        );
        return false;
    }

    DIR* raw_stream = ::fdopendir(scan_descriptor);

    if (!raw_stream) {
        ::close(scan_descriptor);
        set_error(
            error,
            "failed to scan node credential directory"
        );
        return false;
    }

    UniqueDirectoryStream stream(raw_stream);
    std::size_t count = 0;
    std::vector<std::string> temporary_files;
    errno = 0;

    while (dirent* entry = ::readdir(stream.get())) {
        const std::string filename(entry->d_name);

        if (filename == "." || filename == "..") {
            continue;
        }

        ++count;

        if (count > kMaximumCredentialFiles) {
            set_error(
                error,
                "too many node credential files"
            );
            return false;
        }

        if (parse_temporary_credential_filename(
                filename)) {
            const CredentialFileResult temp_result =
                inspect_existing_target(
                    directory_descriptor,
                    filename,
                    nullptr,
                    error
                );

            if (temp_result == CredentialFileResult::Missing) {
                continue;
            }

            if (temp_result != CredentialFileResult::Ok) {
                if (error && error->empty()) {
                    *error =
                        "unsafe node credential temp file";
                }
                return false;
            }

            temporary_files.push_back(filename);
            continue;
        }

        std::string node_id;

        if (!parse_credential_filename(
                filename,
                &node_id)) {
            set_error(
                error,
                "unexpected node credential directory entry"
            );
            return false;
        }

        std::string token;
        SensitiveStringWiper token_wiper(token);

        const CredentialFileResult result =
            read_credential_token(
                directory_descriptor,
                filename,
                &token,
                error
            );

        if (result != CredentialFileResult::Ok) {
            if (error && error->empty()) {
                *error = "invalid node credential registry entry";
            }
            return false;
        }
    }

    if (errno != 0) {
        set_error(
            error,
            "failed while scanning node credential directory"
        );
        return false;
    }

    for (const std::string& filename : temporary_files) {
        const CredentialFileResult recheck_result =
            inspect_existing_target(
                directory_descriptor,
                filename,
                nullptr,
                error
            );

        if (recheck_result == CredentialFileResult::Missing) {
            continue;
        }

        if (recheck_result != CredentialFileResult::Ok) {
            if (error && error->empty()) {
                *error =
                    "unsafe node credential temp file";
            }
            return false;
        }

        // Security: a crash can leave plaintext in the reserved temp namespace.
        // Remove only validated regular single-link files and never follow a
        // replacement target.
        if (::unlinkat(
                directory_descriptor,
                filename.c_str(),
                0) != 0 &&
            errno != ENOENT) {
            set_error(
                error,
                "failed to remove stale node credential temp file"
            );
            return false;
        }
    }

    if (!temporary_files.empty() &&
        ::fsync(directory_descriptor) != 0) {
        set_error(
            error,
            "failed to sync stale credential cleanup"
        );
        return false;
    }

    return true;
}

bool write_all(
    int descriptor,
    const std::string& contents,
    std::string* error
) {
    std::size_t offset = 0;

    while (offset < contents.size()) {
        const ssize_t count = ::write(
            descriptor,
            contents.data() + offset,
            contents.size() - offset
        );

        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }

        if (count < 0 && errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to write node credential"
        );
        return false;
    }

    return true;
}

NodeCredentialRegistryResult
create_credential_file_if_absent(
    int directory_descriptor,
    const std::string& target_filename,
    const std::string& plaintext_token,
    std::string* error
) {
    bool target_exists = false;

    const CredentialFileResult target_result =
        inspect_existing_target(
            directory_descriptor,
            target_filename,
            &target_exists,
            error
        );

    if (target_result == CredentialFileResult::Ok &&
        target_exists) {
        return NodeCredentialRegistryResult::
            AlreadyExists;
    }

    if (target_result == CredentialFileResult::Unsafe) {
        return NodeCredentialRegistryResult::
            StorageUnsafe;
    }

    if (target_result == CredentialFileResult::IoFailed) {
        return NodeCredentialRegistryResult::IoFailed;
    }

    if (target_result != CredentialFileResult::Missing) {
        set_error(
            error,
            "unexpected node credential target state"
        );
        return NodeCredentialRegistryResult::IoFailed;
    }

    // Security: this is the single create-if-absent commit boundary. O_EXCL
    // prevents an existing credential, symlink, or racing insertion from being
    // overwritten or followed.
    const int raw_descriptor =
        ::openat(
            directory_descriptor,
            target_filename.c_str(),
            O_WRONLY |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

    if (raw_descriptor < 0) {
        if (errno == EEXIST) {
            bool raced_target_exists = false;

            const CredentialFileResult
                raced_target_result =
                    inspect_existing_target(
                        directory_descriptor,
                        target_filename,
                        &raced_target_exists,
                        error
                    );

            if (raced_target_result ==
                    CredentialFileResult::Ok &&
                raced_target_exists) {
                return NodeCredentialRegistryResult::
                    AlreadyExists;
            }

            if (raced_target_result ==
                CredentialFileResult::Unsafe) {
                return NodeCredentialRegistryResult::
                    StorageUnsafe;
            }

            if (raced_target_result ==
                CredentialFileResult::IoFailed) {
                return NodeCredentialRegistryResult::
                    IoFailed;
            }

            set_error(
                error,
                "node credential target changed during creation"
            );
            return NodeCredentialRegistryResult::IoFailed;
        }

        set_error(
            error,
            "failed to create node credential"
        );
        return NodeCredentialRegistryResult::
            PersistFailed;
    }

    UniqueFd descriptor(raw_descriptor);

    const auto rollback_created_target = [&]() {
        if (::unlinkat(
                directory_descriptor,
                target_filename.c_str(),
                0) != 0 &&
            errno != ENOENT) {
            set_error(
                error,
                "failed to roll back node credential creation"
            );
            return false;
        }

        if (::fsync(directory_descriptor) != 0) {
            set_error(
                error,
                "failed to sync node credential rollback"
            );
            return false;
        }

        return true;
    };

    std::string contents = plaintext_token;
    SensitiveStringWiper contents_wiper(contents);

    try {
        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to allocate node credential contents"
        );
        (void)rollback_created_target();
        return NodeCredentialRegistryResult::
            PersistFailed;
    }

    if (::fchmod(
            descriptor.get(),
            0600) != 0 ||
        !write_all(
            descriptor.get(),
            contents,
            error) ||
        ::fsync(descriptor.get()) != 0) {
        if (error && error->empty()) {
            *error =
                "failed to sync new node credential";
        }

        (void)rollback_created_target();
        return NodeCredentialRegistryResult::
            PersistFailed;
    }

    const int created_descriptor =
        descriptor.release();

    if (::close(created_descriptor) != 0) {
        set_error(
            error,
            "failed to close new node credential"
        );
        (void)rollback_created_target();
        return NodeCredentialRegistryResult::
            PersistFailed;
    }

    // Security: enrollment advances only after the new directory entry is
    // durable. A sync failure leaves a valid visible file for recovery to
    // inspect, but the current operation still reports an uncertain commit.
    if (::fsync(directory_descriptor) != 0) {
        set_error(
            error,
            "failed to sync new node credential directory"
        );
        return NodeCredentialRegistryResult::
            PersistFailed;
    }

    return NodeCredentialRegistryResult::Ok;
}

NodeCredentialRegistryResult persist_credential_file(
    int directory_descriptor,
    const std::string& target_filename,
    const std::string& plaintext_token,
    std::string* error
) {
    bool target_exists = false;

    const CredentialFileResult target_result =
        inspect_existing_target(
            directory_descriptor,
            target_filename,
            &target_exists,
            error
        );

    (void)target_exists;

    if (target_result == CredentialFileResult::Unsafe) {
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    if (target_result == CredentialFileResult::IoFailed) {
        return NodeCredentialRegistryResult::IoFailed;
    }

    std::string contents = plaintext_token;
    SensitiveStringWiper contents_wiper(contents);

    try {
        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to allocate node credential contents"
        );
        return NodeCredentialRegistryResult::PersistFailed;
    }

    std::string temporary_filename;
    UniqueFd temporary_descriptor;

    for (int attempt = 0; attempt < 16; ++attempt) {
        const std::uint64_t counter =
            g_temp_counter.fetch_add(1);

        temporary_filename =
            "." +
            target_filename +
            ".tmp." +
            std::to_string(
                static_cast<long long>(::getpid())) +
            "." +
            std::to_string(counter);

        const int raw_descriptor = ::openat(
            directory_descriptor,
            temporary_filename.c_str(),
            O_WRONLY |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

        if (raw_descriptor >= 0) {
            temporary_descriptor = UniqueFd(raw_descriptor);
            break;
        }

        if (errno != EEXIST) {
            set_error(
                error,
                "failed to create node credential temp file"
            );
            return NodeCredentialRegistryResult::PersistFailed;
        }
    }

    if (temporary_descriptor.get() < 0) {
        set_error(
            error,
            "failed to allocate node credential temp file"
        );
        return NodeCredentialRegistryResult::PersistFailed;
    }

    const auto remove_temporary = [&]() {
        if (!temporary_filename.empty()) {
            (void)::unlinkat(
                directory_descriptor,
                temporary_filename.c_str(),
                0
            );
        }
    };

    if (::fchmod(
            temporary_descriptor.get(),
            0600) != 0 ||
        !write_all(
            temporary_descriptor.get(),
            contents,
            error) ||
        ::fsync(
            temporary_descriptor.get()) != 0) {
        if (error && error->empty()) {
            *error = "failed to sync node credential";
        }

        remove_temporary();
        return NodeCredentialRegistryResult::PersistFailed;
    }

    const int raw_descriptor =
        temporary_descriptor.release();

    if (::close(raw_descriptor) != 0) {
        set_error(
            error,
            "failed to close node credential temp file"
        );
        remove_temporary();
        return NodeCredentialRegistryResult::PersistFailed;
    }

    const CredentialFileResult recheck_result =
        inspect_existing_target(
            directory_descriptor,
            target_filename,
            nullptr,
            error
        );

    if (recheck_result == CredentialFileResult::Unsafe) {
        remove_temporary();
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    if (recheck_result == CredentialFileResult::IoFailed) {
        remove_temporary();
        return NodeCredentialRegistryResult::IoFailed;
    }

    // Security: renameat commits within the already validated directory and
    // never follows the target. The immediate re-check fails closed on swaps.
    if (::renameat(
            directory_descriptor,
            temporary_filename.c_str(),
            directory_descriptor,
            target_filename.c_str()) != 0) {
        set_error(
            error,
            "failed to commit node credential"
        );
        remove_temporary();
        return NodeCredentialRegistryResult::PersistFailed;
    }

    // Security: enrollment must not consume its bootstrap verifier before the
    // new long-lived credential is durably linked into the directory.
    if (::fsync(directory_descriptor) != 0) {
        set_error(
            error,
            "failed to sync node credential directory"
        );
        return NodeCredentialRegistryResult::PersistFailed;
    }

    return NodeCredentialRegistryResult::Ok;
}

NodeCredentialRegistryResult map_file_result(
    CredentialFileResult result
) {
    switch (result) {
        case CredentialFileResult::Ok:
            return NodeCredentialRegistryResult::Ok;
        case CredentialFileResult::Missing:
            return NodeCredentialRegistryResult::NotFound;
        case CredentialFileResult::Unsafe:
            return NodeCredentialRegistryResult::StorageUnsafe;
        case CredentialFileResult::Invalid:
            return NodeCredentialRegistryResult::CredentialInvalid;
        case CredentialFileResult::IoFailed:
            return NodeCredentialRegistryResult::IoFailed;
    }

    return NodeCredentialRegistryResult::IoFailed;
}

} // namespace

const char* node_credential_registry_result_code(
    NodeCredentialRegistryResult result
) noexcept {
    switch (result) {
        case NodeCredentialRegistryResult::Ok:
            return "ok";
        case NodeCredentialRegistryResult::InvalidInput:
            return "node_credential_invalid_input";
        case NodeCredentialRegistryResult::NotFound:
            return "node_credential_not_found";
        case NodeCredentialRegistryResult::AlreadyExists:
            return "node_credential_already_exists";
        case NodeCredentialRegistryResult::CredentialInvalid:
            return "node_credential_invalid";
        case NodeCredentialRegistryResult::StorageUnsafe:
            return "node_credential_storage_unsafe";
        case NodeCredentialRegistryResult::IoFailed:
            return "node_credential_io_failed";
        case NodeCredentialRegistryResult::PersistFailed:
            return "node_credential_persist_failed";
    }

    return "node_credential_io_failed";
}

bool NodeCredentialRegistry::load(
    const std::string& directory,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::filesystem::path normalized;

    if (!normalize_directory_path(
            directory,
            &normalized,
            error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    UniqueFd descriptor;

    const DirectoryOpenResult open_result =
        open_registry_directory(
            normalized,
            false,
            &descriptor,
            error
        );

    if (open_result == DirectoryOpenResult::Missing) {
        directory_ = std::move(normalized);
        return true;
    }

    if (open_result != DirectoryOpenResult::Ok ||
        !validate_registry_entries(
            descriptor.get(),
            error)) {
        return false;
    }

    directory_ = std::move(normalized);
    return true;
}

NodeCredentialRegistryResult
NodeCredentialRegistry::contains(
    const std::string& node_id,
    bool* present,
    std::string* error
) const {
    if (error) {
        error->clear();
    }

    if (present) {
        *present = false;
    }

    if (!present ||
        !valid_cluster_node_id(node_id)) {
        set_error(
            error,
            "invalid node credential lookup"
        );
        return NodeCredentialRegistryResult::InvalidInput;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (directory_.empty()) {
        set_error(
            error,
            "node credential registry is not loaded"
        );
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    UniqueFd descriptor;

    const DirectoryOpenResult open_result =
        open_registry_directory(
            directory_,
            false,
            &descriptor,
            error
        );

    if (open_result == DirectoryOpenResult::Missing) {
        return NodeCredentialRegistryResult::Ok;
    }

    if (open_result == DirectoryOpenResult::Unsafe) {
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    if (open_result != DirectoryOpenResult::Ok) {
        return NodeCredentialRegistryResult::IoFailed;
    }

    std::string token;
    SensitiveStringWiper token_wiper(token);

    const CredentialFileResult file_result =
        read_credential_token(
            descriptor.get(),
            credential_filename(node_id),
            &token,
            error
        );

    if (file_result == CredentialFileResult::Missing) {
        return NodeCredentialRegistryResult::Ok;
    }

    if (file_result != CredentialFileResult::Ok) {
        return map_file_result(file_result);
    }

    *present = true;
    return NodeCredentialRegistryResult::Ok;
}

NodeCredentialRegistryResult
NodeCredentialRegistry::create_if_absent(
    const std::string& node_id,
    const std::string& plaintext_token,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!valid_cluster_node_id(node_id) ||
        !valid_internal_auth_credential_token(
            plaintext_token)) {
        set_error(
            error,
            "invalid node credential creation"
        );
        return NodeCredentialRegistryResult::
            InvalidInput;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (directory_.empty()) {
        set_error(
            error,
            "node credential registry is not loaded"
        );
        return NodeCredentialRegistryResult::
            StorageUnsafe;
    }

    UniqueFd descriptor;

    const DirectoryOpenResult open_result =
        open_registry_directory(
            directory_,
            true,
            &descriptor,
            error
        );

    if (open_result == DirectoryOpenResult::Unsafe) {
        return NodeCredentialRegistryResult::
            StorageUnsafe;
    }

    if (open_result != DirectoryOpenResult::Ok) {
        return NodeCredentialRegistryResult::IoFailed;
    }

    return create_credential_file_if_absent(
        descriptor.get(),
        credential_filename(node_id),
        plaintext_token,
        error
    );
}

NodeCredentialRegistryResult
NodeCredentialRegistry::upsert(
    const std::string& node_id,
    const std::string& plaintext_token,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!valid_cluster_node_id(node_id) ||
        !valid_internal_auth_credential_token(
            plaintext_token)) {
        set_error(
            error,
            "invalid node credential mutation"
        );
        return NodeCredentialRegistryResult::InvalidInput;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (directory_.empty()) {
        set_error(
            error,
            "node credential registry is not loaded"
        );
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    UniqueFd descriptor;

    const DirectoryOpenResult open_result =
        open_registry_directory(
            directory_,
            true,
            &descriptor,
            error
        );

    if (open_result == DirectoryOpenResult::Unsafe) {
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    if (open_result != DirectoryOpenResult::Ok) {
        return NodeCredentialRegistryResult::IoFailed;
    }

    return persist_credential_file(
        descriptor.get(),
        credential_filename(node_id),
        plaintext_token,
        error
    );
}

NodeCredentialRegistryResult
NodeCredentialRegistry::make_bearer_authorization(
    const std::string& node_id,
    std::string* authorization,
    std::string* error
) const {
    if (error) {
        error->clear();
    }

    if (authorization) {
        clear_internal_auth_token_text(
            authorization
        );
    }

    if (!authorization ||
        !valid_cluster_node_id(node_id)) {
        set_error(
            error,
            "invalid node credential authorization request"
        );
        return NodeCredentialRegistryResult::InvalidInput;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (directory_.empty()) {
        set_error(
            error,
            "node credential registry is not loaded"
        );
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    UniqueFd descriptor;

    const DirectoryOpenResult open_result =
        open_registry_directory(
            directory_,
            false,
            &descriptor,
            error
        );

    if (open_result == DirectoryOpenResult::Missing) {
        return NodeCredentialRegistryResult::NotFound;
    }

    if (open_result == DirectoryOpenResult::Unsafe) {
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    if (open_result != DirectoryOpenResult::Ok) {
        return NodeCredentialRegistryResult::IoFailed;
    }

    std::string token;
    SensitiveStringWiper token_wiper(token);

    const CredentialFileResult file_result =
        read_credential_token(
            descriptor.get(),
            credential_filename(node_id),
            &token,
            error
        );

    if (file_result != CredentialFileResult::Ok) {
        return map_file_result(file_result);
    }

    InternalAuthCredential credential;
    std::string parse_error;

    if (!parse_internal_auth_credential_token(
            token,
            &credential,
            &parse_error)) {
        set_error(
            error,
            "invalid persisted node credential"
        );
        return NodeCredentialRegistryResult::CredentialInvalid;
    }

    if (!make_internal_bearer_authorization(
            credential,
            authorization)) {
        clear_internal_auth_token_text(
            authorization
        );
        set_error(
            error,
            "failed to build node credential authorization"
        );
        return NodeCredentialRegistryResult::CredentialInvalid;
    }

    return NodeCredentialRegistryResult::Ok;
}

NodeCredentialRegistryResult
NodeCredentialRegistry::erase(
    const std::string& node_id,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!valid_cluster_node_id(node_id)) {
        set_error(
            error,
            "invalid node credential deletion"
        );
        return NodeCredentialRegistryResult::InvalidInput;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (directory_.empty()) {
        set_error(
            error,
            "node credential registry is not loaded"
        );
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    UniqueFd descriptor;

    const DirectoryOpenResult open_result =
        open_registry_directory(
            directory_,
            false,
            &descriptor,
            error
        );

    if (open_result == DirectoryOpenResult::Missing) {
        return NodeCredentialRegistryResult::NotFound;
    }

    if (open_result == DirectoryOpenResult::Unsafe) {
        return NodeCredentialRegistryResult::StorageUnsafe;
    }

    if (open_result != DirectoryOpenResult::Ok) {
        return NodeCredentialRegistryResult::IoFailed;
    }

    const std::string filename =
        credential_filename(node_id);

    const CredentialFileResult target_result =
        inspect_existing_target(
            descriptor.get(),
            filename,
            nullptr,
            error
        );

    if (target_result != CredentialFileResult::Ok) {
        return map_file_result(target_result);
    }

    if (::unlinkat(
            descriptor.get(),
            filename.c_str(),
            0) != 0) {
        if (errno == ENOENT) {
            return NodeCredentialRegistryResult::NotFound;
        }

        set_error(
            error,
            "failed to delete node credential"
        );
        return NodeCredentialRegistryResult::PersistFailed;
    }

    // Security: credential revocation is successful only after the directory
    // deletion is durable across a crash or power loss.
    if (::fsync(descriptor.get()) != 0) {
        set_error(
            error,
            "failed to sync node credential deletion"
        );
        return NodeCredentialRegistryResult::PersistFailed;
    }

    return NodeCredentialRegistryResult::Ok;
}

} // namespace pqnas::cluster
