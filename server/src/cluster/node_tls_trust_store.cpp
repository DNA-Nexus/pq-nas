#include "cluster/node_tls_trust_store.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/node_tls_identity.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <dirent.h>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace pqnas::cluster {
namespace {

constexpr std::size_t
    kMaximumTrustStorePathBytes = 4096u;

constexpr std::size_t
    kMaximumTrustFiles = 10000u;

constexpr char kPinSuffix[] =
    ".tls-spki";

constexpr std::size_t kPinSuffixChars =
    sizeof(kPinSuffix) - 1u;

constexpr std::size_t kCanonicalPinChars =
    (sizeof("sha256/") - 1u) +
    kNodeTlsSpkiSha256Base64Chars;

constexpr std::size_t kPinFileBytes =
    kCanonicalPinChars + 1u;

class UniqueFd {
public:
    explicit UniqueFd(
        int descriptor = -1
    ) noexcept
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

    UniqueFd& operator=(
        UniqueFd&& other
    ) noexcept {
        if (this != &other) {
            if (descriptor_ >= 0) {
                ::close(descriptor_);
            }

            descriptor_ =
                other.release();
        }

        return *this;
    }

    int get() const noexcept {
        return descriptor_;
    }

    int release() noexcept {
        const int result =
            descriptor_;

        descriptor_ = -1;
        return result;
    }

private:
    int descriptor_;
};

class UniqueDirectoryStream {
public:
    explicit UniqueDirectoryStream(
        DIR* stream = nullptr
    ) noexcept
        : stream_(stream) {}

    ~UniqueDirectoryStream() {
        if (stream_) {
            ::closedir(stream_);
        }
    }

    UniqueDirectoryStream(
        const UniqueDirectoryStream&
    ) = delete;

    UniqueDirectoryStream& operator=(
        const UniqueDirectoryStream&
    ) = delete;

    DIR* get() const noexcept {
        return stream_;
    }

private:
    DIR* stream_;
};

enum class DirectoryOpenResult {
    Ok,
    Missing,
    Unsafe,
    IoFailed
};

enum class PinFileResult {
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
            "missing node TLS trust directory output"
        );
        return false;
    }

    if (raw.empty() ||
        raw.size() >
            kMaximumTrustStorePathBytes) {
        set_error(
            error,
            "invalid node TLS trust directory path"
        );
        return false;
    }

    const std::filesystem::path path(raw);

    if (!path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            error,
            "node TLS trust directory path must be absolute"
        );
        return false;
    }

    for (const auto& component : path) {
        const std::string part =
            component.string();

        if (part == "." ||
            part == "..") {
            set_error(
                error,
                "node TLS trust directory path contains traversal"
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
            "node TLS trust path must name a directory"
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

    if (::fstat(
            descriptor,
            &stats
        ) != 0) {
        set_error(
            error,
            "failed to inspect node TLS trust directory"
        );
        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    // Security: SPKI pins are public, but their integrity is an authentication
    // boundary. Other local users must not be able to replace trusted node
    // identities.
    if (!S_ISDIR(stats.st_mode) ||
        (stats.st_mode & 0077) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe node TLS trust directory"
        );
        return false;
    }

    return true;
}

DirectoryOpenResult
open_trust_directory(
    const std::filesystem::path& path,
    bool create_if_missing,
    UniqueFd* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing node TLS trust directory descriptor"
        );
        return DirectoryOpenResult::IoFailed;
    }

    bool created = false;

    if (create_if_missing) {
        std::error_code filesystem_error;

        created =
            std::filesystem::create_directories(
                path,
                filesystem_error
            );

        if (filesystem_error) {
            set_error(
                error,
                "failed to create node TLS trust directory"
            );
            return DirectoryOpenResult::IoFailed;
        }
    }

    const int raw_descriptor =
        ::open(
            path.c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_descriptor < 0) {
        if (errno == ENOENT &&
            !create_if_missing) {
            return DirectoryOpenResult::Missing;
        }

        if (errno == ELOOP ||
            errno == ENOTDIR) {
            set_error(
                error,
                "unsafe node TLS trust directory"
            );
            return DirectoryOpenResult::Unsafe;
        }

        set_error(
            error,
            "failed to open node TLS trust directory"
        );
        return DirectoryOpenResult::IoFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    if (created) {
        struct stat created_stats {};

        if (::fstat(
                descriptor.get(),
                &created_stats
            ) != 0 ||
            !S_ISDIR(
                created_stats.st_mode
            ) ||
            created_stats.st_uid !=
                ::geteuid()) {
            set_error(
                error,
                "unsafe newly created node TLS trust directory"
            );
            return DirectoryOpenResult::Unsafe;
        }

        if (::fchmod(
                descriptor.get(),
                0700
            ) != 0) {
            set_error(
                error,
                "failed to secure node TLS trust directory"
            );
            return DirectoryOpenResult::IoFailed;
        }
    }

    if (!validate_directory_descriptor(
            descriptor.get(),
            error)) {
        return DirectoryOpenResult::Unsafe;
    }

    *output =
        std::move(descriptor);

    return DirectoryOpenResult::Ok;
}

std::string pin_filename(
    const std::string& node_id
) {
    return
        node_id +
        kPinSuffix;
}

bool parse_pin_filename(
    const std::string& filename,
    std::string* node_id
) {
    if (!node_id ||
        filename.size() <=
            kPinSuffixChars ||
        filename.compare(
            filename.size() -
                kPinSuffixChars,
            kPinSuffixChars,
            kPinSuffix
        ) != 0) {
        return false;
    }

    const std::string candidate =
        filename.substr(
            0,
            filename.size() -
                kPinSuffixChars
        );

    if (!valid_cluster_node_id(
            candidate)) {
        return false;
    }

    *node_id = candidate;
    return true;
}

bool safe_pin_metadata(
    const struct stat& stats
) {
    const uid_t effective_uid =
        ::geteuid();

    return
        S_ISREG(stats.st_mode) &&
        stats.st_nlink == 1 &&
        (stats.st_mode & 0077) == 0 &&
        (stats.st_uid == effective_uid ||
         stats.st_uid == 0);
}

bool canonical_pin(
    const std::string& value
) {
    std::string normalized;

    return
        normalize_node_tls_spki_sha256_pin(
            value,
            &normalized,
            nullptr
        ) &&
        normalized == value;
}

bool read_all_bounded(
    int descriptor,
    std::string* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing node TLS pin output"
        );
        return false;
    }

    output->clear();

    std::array<
        char,
        kPinFileBytes + 1u
    > buffer{};

    std::size_t total = 0;

    while (total < buffer.size()) {
        const ssize_t count =
            ::read(
                descriptor,
                buffer.data() + total,
                buffer.size() - total
            );

        if (count > 0) {
            total +=
                static_cast<std::size_t>(
                    count
                );
            continue;
        }

        if (count == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to read node TLS pin"
        );
        return false;
    }

    if (total > kPinFileBytes) {
        set_error(
            error,
            "node TLS pin file is too large"
        );
        return false;
    }

    output->assign(
        buffer.data(),
        total
    );

    return true;
}

PinFileResult read_pin(
    int directory_descriptor,
    const std::string& filename,
    std::string* pin,
    std::string* error
) {
    if (!pin) {
        set_error(
            error,
            "missing node TLS pin output"
        );
        return PinFileResult::IoFailed;
    }

    pin->clear();

    const int raw_descriptor =
        ::openat(
            directory_descriptor,
            filename.c_str(),
            O_RDONLY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_descriptor < 0) {
        if (errno == ENOENT) {
            return PinFileResult::Missing;
        }

        if (errno == ELOOP) {
            set_error(
                error,
                "unsafe node TLS pin target"
            );
            return PinFileResult::Unsafe;
        }

        set_error(
            error,
            "failed to open node TLS pin"
        );
        return PinFileResult::IoFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    struct stat stats {};

    if (::fstat(
            descriptor.get(),
            &stats
        ) != 0) {
        set_error(
            error,
            "failed to inspect node TLS pin"
        );
        return PinFileResult::IoFailed;
    }

    if (!safe_pin_metadata(stats)) {
        set_error(
            error,
            "unsafe node TLS pin target"
        );
        return PinFileResult::Unsafe;
    }

    if (stats.st_size !=
        static_cast<off_t>(
            kPinFileBytes
        )) {
        set_error(
            error,
            "invalid node TLS pin file size"
        );
        return PinFileResult::Invalid;
    }

    std::string contents;

    if (!read_all_bounded(
            descriptor.get(),
            &contents,
            error)) {
        return PinFileResult::IoFailed;
    }

    if (contents.size() !=
            kPinFileBytes ||
        contents.back() != '\n') {
        set_error(
            error,
            "invalid node TLS pin file"
        );
        return PinFileResult::Invalid;
    }

    contents.pop_back();

    if (contents.find('\n') !=
            std::string::npos ||
        contents.find('\r') !=
            std::string::npos ||
        !canonical_pin(contents)) {
        set_error(
            error,
            "invalid node TLS SPKI pin"
        );
        return PinFileResult::Invalid;
    }

    *pin =
        std::move(contents);

    return PinFileResult::Ok;
}

NodeTlsTrustStoreResult
map_pin_result(
    PinFileResult result
) {
    switch (result) {
        case PinFileResult::Ok:
            return
                NodeTlsTrustStoreResult::Ok;

        case PinFileResult::Missing:
            return
                NodeTlsTrustStoreResult::NotFound;

        case PinFileResult::Unsafe:
            return
                NodeTlsTrustStoreResult::StorageUnsafe;

        case PinFileResult::Invalid:
            return
                NodeTlsTrustStoreResult::PinInvalid;

        case PinFileResult::IoFailed:
            return
                NodeTlsTrustStoreResult::IoFailed;
    }

    return
        NodeTlsTrustStoreResult::IoFailed;
}

bool validate_store_entries(
    int directory_descriptor,
    std::string* error
) {
    const int scan_descriptor =
        ::fcntl(
            directory_descriptor,
            F_DUPFD_CLOEXEC,
            0
        );

    if (scan_descriptor < 0) {
        set_error(
            error,
            "failed to duplicate node TLS trust directory"
        );
        return false;
    }

    DIR* raw_stream =
        ::fdopendir(
            scan_descriptor
        );

    if (!raw_stream) {
        ::close(scan_descriptor);

        set_error(
            error,
            "failed to scan node TLS trust directory"
        );
        return false;
    }

    UniqueDirectoryStream stream(
        raw_stream
    );

    std::size_t count = 0;
    errno = 0;

    while (dirent* entry =
               ::readdir(
                   stream.get()
               )) {
        const std::string filename(
            entry->d_name
        );

        if (filename == "." ||
            filename == "..") {
            continue;
        }

        ++count;

        if (count >
            kMaximumTrustFiles) {
            set_error(
                error,
                "too many node TLS trust files"
            );
            return false;
        }

        std::string node_id;

        if (!parse_pin_filename(
                filename,
                &node_id)) {
            set_error(
                error,
                "unexpected node TLS trust directory entry"
            );
            return false;
        }

        std::string pin;

        const PinFileResult result =
            read_pin(
                directory_descriptor,
                filename,
                &pin,
                error
            );

        if (result !=
            PinFileResult::Ok) {
            if (error &&
                error->empty()) {
                *error =
                    "invalid node TLS trust entry";
            }

            return false;
        }
    }

    if (errno != 0) {
        set_error(
            error,
            "failed while scanning node TLS trust directory"
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

    while (offset <
           contents.size()) {
        const ssize_t count =
            ::write(
                descriptor,
                contents.data() + offset,
                contents.size() - offset
            );

        if (count > 0) {
            offset +=
                static_cast<std::size_t>(
                    count
                );
            continue;
        }

        if (count < 0 &&
            errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to write node TLS pin"
        );
        return false;
    }

    return true;
}

NodeTlsTrustStoreResult
existing_binding_result(
    int directory_descriptor,
    const std::string& filename,
    const std::string& expected_pin,
    std::string* error
) {
    std::string current_pin;

    const PinFileResult file_result =
        read_pin(
            directory_descriptor,
            filename,
            &current_pin,
            error
        );

    if (file_result ==
        PinFileResult::Missing) {
        return
            NodeTlsTrustStoreResult::NotFound;
    }

    if (file_result !=
        PinFileResult::Ok) {
        return
            map_pin_result(
                file_result
            );
    }

    // Security: an already bound TLS identity is immutable through this API.
    // A bearer credential or enrollment retry must never replace it.
    if (current_pin !=
        expected_pin) {
        set_error(
            error,
            "node TLS identity is already bound to a different pin"
        );

        return
            NodeTlsTrustStoreResult::
                TrustConflict;
    }

    // Security: this also resolves a retry after the original create became
    // visible but its parent-directory fsync reported failure. Never delete or
    // replace the visible same identity merely because durability was uncertain.
    if (::fsync(
            directory_descriptor
        ) != 0) {
        set_error(
            error,
            "failed to sync existing node TLS trust binding"
        );

        return
            NodeTlsTrustStoreResult::
                PersistFailed;
    }

    return
        NodeTlsTrustStoreResult::Ok;
}

NodeTlsTrustStoreResult
create_binding_if_absent(
    int directory_descriptor,
    const std::string& filename,
    const std::string& pin,
    std::string* error
) {
    const NodeTlsTrustStoreResult
        existing_result =
            existing_binding_result(
                directory_descriptor,
                filename,
                pin,
                error
            );

    if (existing_result !=
        NodeTlsTrustStoreResult::
            NotFound) {
        return existing_result;
    }

    // Security: immutable first binding uses O_EXCL so neither a racing
    // enrollment nor an attacker-controlled existing path can be overwritten.
    // TLS identity rotation is intentionally not implemented by this API.
    const int raw_descriptor =
        ::openat(
            directory_descriptor,
            filename.c_str(),
            O_WRONLY |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

    if (raw_descriptor < 0) {
        if (errno == EEXIST) {
            return
                existing_binding_result(
                    directory_descriptor,
                    filename,
                    pin,
                    error
                );
        }

        set_error(
            error,
            "failed to create node TLS trust binding"
        );

        return
            NodeTlsTrustStoreResult::
                PersistFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    const auto rollback_created =
        [&]() {
            if (::unlinkat(
                    directory_descriptor,
                    filename.c_str(),
                    0
                ) != 0 &&
                errno != ENOENT) {
                set_error(
                    error,
                    "failed to roll back node TLS trust binding"
                );
                return false;
            }

            if (::fsync(
                    directory_descriptor
                ) != 0) {
                set_error(
                    error,
                    "failed to sync node TLS trust rollback"
                );
                return false;
            }

            return true;
        };

    std::string contents = pin;

    try {
        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to allocate node TLS trust contents"
        );

        (void)rollback_created();

        return
            NodeTlsTrustStoreResult::
                PersistFailed;
    }

    if (::fchmod(
            descriptor.get(),
            0600
        ) != 0 ||
        !write_all(
            descriptor.get(),
            contents,
            error
        ) ||
        ::fsync(
            descriptor.get()
        ) != 0) {
        if (error &&
            error->empty()) {
            *error =
                "failed to sync new node TLS trust binding";
        }

        (void)rollback_created();

        return
            NodeTlsTrustStoreResult::
                PersistFailed;
    }

    const int created_descriptor =
        descriptor.release();

    if (::close(
            created_descriptor
        ) != 0) {
        set_error(
            error,
            "failed to close new node TLS trust binding"
        );

        (void)rollback_created();

        return
            NodeTlsTrustStoreResult::
                PersistFailed;
    }

    // Security: the first binding becomes usable only after its directory entry
    // is confirmed durable. On failure the visible binding is retained so an
    // exact retry can re-fsync it; it is never replaced with another pin.
    if (::fsync(
            directory_descriptor
        ) != 0) {
        set_error(
            error,
            "failed to sync new node TLS trust directory"
        );

        return
            NodeTlsTrustStoreResult::
                PersistFailed;
    }

    return
        NodeTlsTrustStoreResult::Ok;
}

} // namespace

const char*
node_tls_trust_store_result_code(
    NodeTlsTrustStoreResult result
) noexcept {
    switch (result) {
        case NodeTlsTrustStoreResult::Ok:
            return "ok";

        case NodeTlsTrustStoreResult::InvalidInput:
            return "invalid_input";

        case NodeTlsTrustStoreResult::NotFound:
            return "not_found";

        case NodeTlsTrustStoreResult::TrustConflict:
            return "trust_conflict";

        case NodeTlsTrustStoreResult::PinInvalid:
            return "pin_invalid";

        case NodeTlsTrustStoreResult::StorageUnsafe:
            return "storage_unsafe";

        case NodeTlsTrustStoreResult::IoFailed:
            return "io_failed";

        case NodeTlsTrustStoreResult::PersistFailed:
            return "persist_failed";
    }

    return "unknown";
}

bool NodeTlsTrustStore::load(
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

    std::lock_guard<std::mutex>
        lock(mutex_);

    UniqueFd descriptor;

    const DirectoryOpenResult
        open_result =
            open_trust_directory(
                normalized,
                false,
                &descriptor,
                error
            );

    if (open_result ==
        DirectoryOpenResult::Missing) {
        directory_ =
            std::move(normalized);

        return true;
    }

    if (open_result !=
            DirectoryOpenResult::Ok ||
        !validate_store_entries(
            descriptor.get(),
            error)) {
        return false;
    }

    directory_ =
        std::move(normalized);

    return true;
}

NodeTlsTrustStoreResult
NodeTlsTrustStore::get(
    const std::string& node_id,
    std::string* tls_spki_sha256_pin,
    std::string* error
) const {
    if (error) {
        error->clear();
    }

    if (tls_spki_sha256_pin) {
        tls_spki_sha256_pin->clear();
    }

    if (!tls_spki_sha256_pin ||
        !valid_cluster_node_id(
            node_id)) {
        set_error(
            error,
            "invalid node TLS trust lookup"
        );

        return
            NodeTlsTrustStoreResult::
                InvalidInput;
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    if (directory_.empty()) {
        set_error(
            error,
            "node TLS trust store is not loaded"
        );

        return
            NodeTlsTrustStoreResult::
                StorageUnsafe;
    }

    UniqueFd descriptor;

    const DirectoryOpenResult
        open_result =
            open_trust_directory(
                directory_,
                false,
                &descriptor,
                error
            );

    if (open_result ==
        DirectoryOpenResult::Missing) {
        return
            NodeTlsTrustStoreResult::
                NotFound;
    }

    if (open_result ==
        DirectoryOpenResult::Unsafe) {
        return
            NodeTlsTrustStoreResult::
                StorageUnsafe;
    }

    if (open_result !=
        DirectoryOpenResult::Ok) {
        return
            NodeTlsTrustStoreResult::
                IoFailed;
    }

    const PinFileResult file_result =
        read_pin(
            descriptor.get(),
            pin_filename(node_id),
            tls_spki_sha256_pin,
            error
        );

    return
        map_pin_result(
            file_result
        );
}

NodeTlsTrustStoreResult
NodeTlsTrustStore::bind_if_absent(
    const std::string& node_id,
    const std::string& tls_spki_sha256_pin,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!valid_cluster_node_id(
            node_id)) {
        set_error(
            error,
            "invalid node TLS trust binding"
        );

        return
            NodeTlsTrustStoreResult::
                InvalidInput;
    }

    if (!canonical_pin(
            tls_spki_sha256_pin)) {
        set_error(
            error,
            "invalid node TLS SPKI pin"
        );

        return
            NodeTlsTrustStoreResult::
                PinInvalid;
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    if (directory_.empty()) {
        set_error(
            error,
            "node TLS trust store is not loaded"
        );

        return
            NodeTlsTrustStoreResult::
                StorageUnsafe;
    }

    UniqueFd descriptor;

    const DirectoryOpenResult
        open_result =
            open_trust_directory(
                directory_,
                true,
                &descriptor,
                error
            );

    if (open_result ==
        DirectoryOpenResult::Unsafe) {
        return
            NodeTlsTrustStoreResult::
                StorageUnsafe;
    }

    if (open_result !=
        DirectoryOpenResult::Ok) {
        return
            NodeTlsTrustStoreResult::
                IoFailed;
    }

    return
        create_binding_if_absent(
            descriptor.get(),
            pin_filename(node_id),
            tls_spki_sha256_pin,
            error
        );
}

} // namespace pqnas::cluster
