#include "cluster/storage_node_credential_runtime.h"

#include <array>
#include <cerrno>
#include <cstddef>
#include <fcntl.h>
#include <filesystem>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace pqnas::cluster {
namespace {

constexpr std::size_t kMaximumPathBytes = 4096;
constexpr std::size_t kMaximumFilenameBytes = 255;
constexpr std::size_t kMaximumCredentialFileBytes = 128;

constexpr char kPendingSuffix[] = ".pending";
constexpr char kTemporarySuffix[] = ".rotate.tmp";

class UniqueFd {
public:
    explicit UniqueFd(
        int descriptor = -1
    ) noexcept
        : descriptor_(descriptor) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(
        const UniqueFd&
    ) = delete;

    UniqueFd& operator=(
        const UniqueFd&
    ) = delete;

    UniqueFd(
        UniqueFd&& other
    ) noexcept
        : descriptor_(other.release()) {}

    UniqueFd& operator=(
        UniqueFd&& other
    ) noexcept {
        if (this != &other) {
            reset(
                other.release()
            );
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

    void reset(
        int descriptor = -1
    ) noexcept {
        if (descriptor_ >= 0) {
            (void)::close(
                descriptor_
            );
        }

        descriptor_ =
            descriptor;
    }

private:
    int descriptor_;
};

class SensitiveStringWiper {
public:
    explicit SensitiveStringWiper(
        std::string& value
    ) noexcept
        : value_(value) {}

    ~SensitiveStringWiper() {
        clear_internal_auth_token_text(
            &value_
        );
    }

    SensitiveStringWiper(
        const SensitiveStringWiper&
    ) = delete;

    SensitiveStringWiper& operator=(
        const SensitiveStringWiper&
    ) = delete;

private:
    std::string& value_;
};

enum class EntryResult {
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

bool safe_owner(
    uid_t owner
) noexcept {
    const uid_t effective_uid =
        ::geteuid();

    return
        owner == effective_uid ||
        owner == 0;
}

bool safe_directory_metadata(
    const struct stat& stats
) noexcept {
    return
        S_ISDIR(stats.st_mode) &&
        safe_owner(stats.st_uid) &&
        (stats.st_mode & 0077) == 0;
}

bool safe_regular_metadata(
    const struct stat& stats,
    std::size_t maximum_size
) noexcept {
    return
        S_ISREG(stats.st_mode) &&
        safe_owner(stats.st_uid) &&
        stats.st_nlink == 1 &&
        (stats.st_mode & 0077) == 0 &&
        stats.st_size >= 0 &&
        static_cast<unsigned long long>(
            stats.st_size
        ) <=
            static_cast<unsigned long long>(
                maximum_size
            );
}

bool normalize_active_path(
    const std::string& text,
    std::filesystem::path* directory,
    std::string* active_filename,
    std::string* pending_filename,
    std::string* temporary_filename,
    std::string* error
) {
    if (!directory ||
        !active_filename ||
        !pending_filename ||
        !temporary_filename) {
        set_error(
            error,
            "missing credential runtime path output"
        );

        return false;
    }

    const std::filesystem::path path(
        text
    );

    if (text.empty() ||
        text.size() >
            kMaximumPathBytes ||
        !path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            error,
            "credential runtime path must be a bounded absolute path"
        );

        return false;
    }

    for (const auto& component :
         path) {
        const std::string part =
            component.string();

        if (part == "." ||
            part == "..") {
            set_error(
                error,
                "credential runtime path contains a dot component"
            );

            return false;
        }
    }

    const std::filesystem::path clean =
        path.lexically_normal();

    if (clean.empty() ||
        clean != path ||
        clean == clean.root_path() ||
        clean.filename().empty()) {
        set_error(
            error,
            "credential runtime path must name one canonical file"
        );

        return false;
    }

    const std::filesystem::path parent =
        clean.parent_path();

    if (parent.empty() ||
        parent == parent.root_path()) {
        set_error(
            error,
            "credential runtime file must be below a private directory"
        );

        return false;
    }

    const std::string filename =
        clean.filename().string();

    const std::string pending =
        filename +
        kPendingSuffix;

    const std::string temporary =
        filename +
        kTemporarySuffix;

    if (filename.empty() ||
        filename.size() >
            kMaximumFilenameBytes ||
        pending.size() >
            kMaximumFilenameBytes ||
        temporary.size() >
            kMaximumFilenameBytes) {
        set_error(
            error,
            "credential runtime filename is too long"
        );

        return false;
    }

    *directory =
        parent;

    *active_filename =
        filename;

    *pending_filename =
        pending;

    *temporary_filename =
        temporary;

    return true;
}

bool open_private_directory(
    const std::filesystem::path& path,
    UniqueFd* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing credential directory output"
        );

        return false;
    }

    struct stat path_stats {};

    if (::lstat(
            path.c_str(),
            &path_stats) != 0) {
        set_error(
            error,
            "failed to inspect credential directory"
        );

        return false;
    }

    if (!safe_directory_metadata(
            path_stats)) {
        set_error(
            error,
            "unsafe credential directory"
        );

        return false;
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
        set_error(
            error,
            "failed to open credential directory"
        );

        return false;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    struct stat opened_stats {};

    if (::fstat(
            descriptor.get(),
            &opened_stats) != 0 ||
        !safe_directory_metadata(
            opened_stats)) {
        set_error(
            error,
            "unsafe opened credential directory"
        );

        return false;
    }

    *output =
        std::move(
            descriptor
        );

    return true;
}

EntryResult inspect_entry(
    int directory_descriptor,
    const std::string& filename,
    std::string* error
) {
    struct stat stats {};

    if (::fstatat(
            directory_descriptor,
            filename.c_str(),
            &stats,
            AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return
                EntryResult::Missing;
        }

        set_error(
            error,
            "failed to inspect credential runtime file"
        );

        return
            EntryResult::IoFailed;
    }

    if (S_ISLNK(stats.st_mode) ||
        !safe_regular_metadata(
            stats,
            kMaximumCredentialFileBytes)) {
        set_error(
            error,
            "unsafe credential runtime file"
        );

        return
            EntryResult::Unsafe;
    }

    return
        EntryResult::Ok;
}

EntryResult read_credential_at(
    int directory_descriptor,
    const std::string& filename,
    std::string* token,
    InternalAuthCredential* credential,
    std::string* error
) {
    if (!token ||
        !credential) {
        set_error(
            error,
            "missing credential read output"
        );

        return
            EntryResult::Invalid;
    }

    clear_internal_auth_token_text(
        token
    );

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
            return
                EntryResult::Missing;
        }

        set_error(
            error,
            errno == ELOOP
                ? "credential runtime file must not be a symlink"
                : "failed to open credential runtime file"
        );

        return
            errno == ELOOP
                ? EntryResult::Unsafe
                : EntryResult::IoFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    struct stat stats {};

    if (::fstat(
            descriptor.get(),
            &stats) != 0) {
        set_error(
            error,
            "failed to inspect opened credential runtime file"
        );

        return
            EntryResult::IoFailed;
    }

    if (!safe_regular_metadata(
            stats,
            kMaximumCredentialFileBytes)) {
        set_error(
            error,
            "unsafe opened credential runtime file"
        );

        return
            EntryResult::Unsafe;
    }

    std::array<
        char,
        kMaximumCredentialFileBytes + 1
    > buffer{};

    std::size_t total = 0;

    while (total <
           buffer.size()) {
        const ssize_t count =
            ::read(
                descriptor.get(),
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
            "failed to read credential runtime file"
        );

        return
            EntryResult::IoFailed;
    }

    if (total >
        kMaximumCredentialFileBytes) {
        set_error(
            error,
            "credential runtime file is too large"
        );

        return
            EntryResult::Invalid;
    }

    try {
        token->assign(
            buffer.data(),
            total
        );
    } catch (...) {
        set_error(
            error,
            "failed to allocate credential runtime input"
        );

        return
            EntryResult::IoFailed;
    }

    if (!token->empty() &&
        token->back() == '\n') {
        token->pop_back();

        if (!token->empty() &&
            token->back() == '\r') {
            token->pop_back();
        }
    }

    if (token->find('\n') !=
            std::string::npos ||
        token->find('\r') !=
            std::string::npos ||
        !valid_internal_auth_credential_token(
            *token)) {
        clear_internal_auth_token_text(
            token
        );

        set_error(
            error,
            "invalid credential runtime file"
        );

        return
            EntryResult::Invalid;
    }

    std::string parse_error;

    if (!parse_internal_auth_credential_token(
            *token,
            credential,
            &parse_error)) {
        clear_internal_auth_token_text(
            token
        );

        set_error(
            error,
            "invalid credential runtime token"
        );

        return
            EntryResult::Invalid;
    }

    return
        EntryResult::Ok;
}

StorageNodeCredentialRuntimeResult
map_entry_result(
    EntryResult result
) noexcept {
    switch (result) {
        case EntryResult::Ok:
            return
                StorageNodeCredentialRuntimeResult::
                    Ok;

        case EntryResult::Missing:
            return
                StorageNodeCredentialRuntimeResult::
                    NoPending;

        case EntryResult::Unsafe:
            return
                StorageNodeCredentialRuntimeResult::
                    StorageUnsafe;

        case EntryResult::Invalid:
            return
                StorageNodeCredentialRuntimeResult::
                    CredentialInvalid;

        case EntryResult::IoFailed:
            return
                StorageNodeCredentialRuntimeResult::
                    IoFailed;
    }

    return
        StorageNodeCredentialRuntimeResult::
            IoFailed;
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
            "failed to write credential runtime file"
        );

        return false;
    }

    return true;
}

void rollback_created_entry(
    int directory_descriptor,
    const std::string& filename
) noexcept {
    (void)::unlinkat(
        directory_descriptor,
        filename.c_str(),
        0
    );

    // Security: best-effort synchronization prevents a failed create from
    // being silently resurrected after a crash.
    (void)::fsync(
        directory_descriptor
    );
}

StorageNodeCredentialRuntimeResult
create_credential_entry(
    int directory_descriptor,
    const std::string& filename,
    const std::string& token,
    std::string* error
) {
    if (!valid_internal_auth_credential_token(
            token)) {
        set_error(
            error,
            "refusing to persist invalid credential token"
        );

        return
            StorageNodeCredentialRuntimeResult::
                CredentialInvalid;
    }

    std::string contents =
        token;

    SensitiveStringWiper
        contents_wiper(contents);

    try {
        contents.push_back(
            '\n'
        );
    } catch (...) {
        set_error(
            error,
            "failed to allocate credential contents"
        );

        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

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
        set_error(
            error,
            errno == EEXIST
                ? "credential rotation file already exists"
                : "failed to create credential rotation file"
        );

        return
            errno == EEXIST
                ? StorageNodeCredentialRuntimeResult::
                      StorageUnsafe
                : StorageNodeCredentialRuntimeResult::
                      PersistFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    if (::fchmod(
            descriptor.get(),
            0600) != 0 ||
        !write_all(
            descriptor.get(),
            contents,
            error) ||
        ::fsync(
            descriptor.get()) != 0) {
        if (error &&
            error->empty()) {
            *error =
                "failed to sync credential rotation file";
        }

        rollback_created_entry(
            directory_descriptor,
            filename
        );

        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

    if (::close(
            descriptor.release()) != 0) {
        set_error(
            error,
            "failed to close credential rotation file"
        );

        rollback_created_entry(
            directory_descriptor,
            filename
        );

        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

    // Security: a credential does not become authoritative until its directory
    // entry is durably synchronized.
    if (::fsync(
            directory_descriptor) != 0) {
        set_error(
            error,
            "failed to sync credential rotation directory"
        );

        // Leave the completely written file visible. A retry can validate it,
        // fsync it and the directory, and recover the exact same credential.
        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

    return
        StorageNodeCredentialRuntimeResult::
            Ok;
}

StorageNodeCredentialRuntimeResult
sync_existing_entry(
    int directory_descriptor,
    const std::string& filename,
    std::string* error
) {
    const int raw_descriptor =
        ::openat(
            directory_descriptor,
            filename.c_str(),
            O_RDONLY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_descriptor < 0) {
        set_error(
            error,
            "failed to reopen credential rotation file"
        );

        return
            errno == ELOOP
                ? StorageNodeCredentialRuntimeResult::
                      StorageUnsafe
                : StorageNodeCredentialRuntimeResult::
                      IoFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    struct stat stats {};

    if (::fstat(
            descriptor.get(),
            &stats) != 0) {
        set_error(
            error,
            "failed to inspect credential rotation file"
        );

        return
            StorageNodeCredentialRuntimeResult::
                IoFailed;
    }

    if (!safe_regular_metadata(
            stats,
            kMaximumCredentialFileBytes)) {
        set_error(
            error,
            "unsafe credential rotation file"
        );

        return
            StorageNodeCredentialRuntimeResult::
                StorageUnsafe;
    }

    // Security: this establishes file-content durability when recovering a
    // fully written pending file from an earlier uncertain create.
    if (::fsync(
            descriptor.get()) != 0 ||
        ::fsync(
            directory_descriptor) != 0) {
        set_error(
            error,
            "failed to synchronize recovered credential rotation"
        );

        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

    return
        StorageNodeCredentialRuntimeResult::
            Ok;
}

StorageNodeCredentialRuntimeResult
cleanup_temporary_entry(
    int directory_descriptor,
    const std::string& temporary_filename,
    std::string* error
) {
    const EntryResult inspect_result =
        inspect_entry(
            directory_descriptor,
            temporary_filename,
            error
        );

    if (inspect_result ==
        EntryResult::Missing) {
        return
            StorageNodeCredentialRuntimeResult::
                Ok;
    }

    if (inspect_result !=
        EntryResult::Ok) {
        // Security: never unlink an unexpected symlink, hard link, device or
        // otherwise unsafe object merely because it has the temp filename.
        return
            StorageNodeCredentialRuntimeResult::
                StorageUnsafe;
    }

    if (::unlinkat(
            directory_descriptor,
            temporary_filename.c_str(),
            0) != 0) {
        set_error(
            error,
            "failed to remove stale credential temporary file"
        );

        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

    if (::fsync(
            directory_descriptor) != 0) {
        set_error(
            error,
            "failed to sync stale credential temporary cleanup"
        );

        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

    return
        StorageNodeCredentialRuntimeResult::
            Ok;
}

bool token_matches_credential(
    const std::string& token,
    const InternalAuthCredential& credential
) {
    if (!credential.configured() ||
        !valid_internal_auth_credential_token(
            token)) {
        return false;
    }

    std::string authorization;
    SensitiveStringWiper
        authorization_wiper(
            authorization
        );

    try {
        authorization.reserve(
            sizeof("Bearer ") - 1 +
            token.size()
        );

        authorization.assign(
            "Bearer "
        );

        authorization.append(
            token
        );
    } catch (...) {
        return false;
    }

    return
        verify_internal_bearer_authorization(
            authorization,
            credential
        );
}

bool parse_candidate(
    const std::string& token,
    InternalAuthCredential* credential,
    std::string* error
) {
    std::string parse_error;

    if (!parse_internal_auth_credential_token(
            token,
            credential,
            &parse_error)) {
        set_error(
            error,
            "invalid credential rotation token"
        );

        return false;
    }

    return true;
}

} // namespace

const char*
storage_node_credential_runtime_result_code(
    StorageNodeCredentialRuntimeResult result
) noexcept {
    switch (result) {
        case StorageNodeCredentialRuntimeResult::Ok:
            return "ok";

        case StorageNodeCredentialRuntimeResult::InvalidInput:
            return "invalid_input";

        case StorageNodeCredentialRuntimeResult::NotLoaded:
            return "not_loaded";

        case StorageNodeCredentialRuntimeResult::AuthenticationFailed:
            return "authentication_failed";

        case StorageNodeCredentialRuntimeResult::NoPending:
            return "no_pending";

        case StorageNodeCredentialRuntimeResult::StateMismatch:
            return "state_mismatch";

        case StorageNodeCredentialRuntimeResult::CredentialInvalid:
            return "credential_invalid";

        case StorageNodeCredentialRuntimeResult::StorageUnsafe:
            return "storage_unsafe";

        case StorageNodeCredentialRuntimeResult::IoFailed:
            return "io_failed";

        case StorageNodeCredentialRuntimeResult::PersistFailed:
            return "persist_failed";
    }

    return "unknown";
}

StorageNodeCredentialRuntime::
~StorageNodeCredentialRuntime() {
    reset_unlocked();
}

void
StorageNodeCredentialRuntime::
reset_unlocked() noexcept {
    active_.clear();
    pending_.clear();

    if (directory_descriptor_ >= 0) {
        (void)::close(
            directory_descriptor_
        );
    }

    directory_descriptor_ = -1;

    directory_.clear();
    active_filename_.clear();
    pending_filename_.clear();
    temporary_filename_.clear();

    loaded_ = false;
    pending_matches_active_ = false;
    pending_unlink_uncertain_ = false;
}

bool
StorageNodeCredentialRuntime::
load(
    const std::string& active_credential_file,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    reset_unlocked();

    std::filesystem::path directory;
    std::string active_filename;
    std::string pending_filename;
    std::string temporary_filename;

    if (!normalize_active_path(
            active_credential_file,
            &directory,
            &active_filename,
            &pending_filename,
            &temporary_filename,
            error)) {
        return false;
    }

    UniqueFd descriptor;

    if (!open_private_directory(
            directory,
            &descriptor,
            error)) {
        return false;
    }

    if (cleanup_temporary_entry(
            descriptor.get(),
            temporary_filename,
            error) !=
        StorageNodeCredentialRuntimeResult::
            Ok) {
        return false;
    }

    std::string active_token;
    SensitiveStringWiper
        active_token_wiper(
            active_token
        );

    InternalAuthCredential
        active_candidate;

    const EntryResult active_result =
        read_credential_at(
            descriptor.get(),
            active_filename,
            &active_token,
            &active_candidate,
            error
        );

    if (active_result !=
        EntryResult::Ok) {
        if (active_result ==
            EntryResult::Missing) {
            set_error(
                error,
                "active storage node credential is missing"
            );
        }

        return false;
    }

    std::string pending_token;
    SensitiveStringWiper
        pending_token_wiper(
            pending_token
        );

    InternalAuthCredential
        pending_candidate;

    const EntryResult pending_result =
        read_credential_at(
            descriptor.get(),
            pending_filename,
            &pending_token,
            &pending_candidate,
            error
        );

    if (pending_result !=
            EntryResult::Ok &&
        pending_result !=
            EntryResult::Missing) {
        return false;
    }

    if (pending_result ==
        EntryResult::Ok) {
        // Security: a visible pending file may come from a process that
        // crashed after fsync(file) but before fsync(parent). Do not make NEW
        // acceptable after restart until both the file contents and its
        // directory entry have been durably synchronized.
        const auto sync_result =
            sync_existing_entry(
                descriptor.get(),
                pending_filename,
                error
            );

        if (sync_result !=
            StorageNodeCredentialRuntimeResult::
                Ok) {
            return false;
        }
    }

    directory_ =
        std::move(
            directory
        );

    active_filename_ =
        std::move(
            active_filename
        );

    pending_filename_ =
        std::move(
            pending_filename
        );

    temporary_filename_ =
        std::move(
            temporary_filename
        );

    directory_descriptor_ =
        descriptor.release();

    active_ =
        std::move(
            active_candidate
        );

    if (pending_result ==
        EntryResult::Ok) {
        pending_ =
            std::move(
                pending_candidate
            );

        pending_matches_active_ =
            token_matches_credential(
                pending_token,
                active_
            );

        if (pending_matches_active_) {
            // Security: ACTIVE == PENDING after restart can mean the
            // replacement rename became visible before its parent-directory
            // fsync completed. Re-synchronize ACTIVE before treating the
            // promoted credential as durably authoritative or allowing the
            // pending recovery anchor to be removed.
            const auto active_sync_result =
                sync_existing_entry(
                    directory_descriptor_,
                    active_filename_,
                    error
                );

            if (active_sync_result !=
                StorageNodeCredentialRuntimeResult::
                    Ok) {
                reset_unlocked();
                return false;
            }
        }
    }

    loaded_ = true;

    return true;
}

bool
StorageNodeCredentialRuntime::
verify_authorization(
    const std::string& authorization
) const {
    std::lock_guard<std::mutex>
        lock(mutex_);

    if (!loaded_) {
        return false;
    }

    // Security: execute both credential checks. Route code sees only one
    // boolean result and never learns which generation matched.
    const bool active_match =
        verify_internal_bearer_authorization(
            authorization,
            active_
        );

    const bool pending_match =
        verify_internal_bearer_authorization(
            authorization,
            pending_
        );

    return
        active_match ||
        pending_match;
}

StorageNodeCredentialRuntimeResult
StorageNodeCredentialRuntime::
prepare_rotation_authorized(
    const std::string& authorization,
    std::string* pending_token,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!pending_token) {
        return
            StorageNodeCredentialRuntimeResult::
                InvalidInput;
    }

    clear_internal_auth_token_text(
        pending_token
    );

    std::lock_guard<std::mutex>
        lock(mutex_);

    if (!loaded_) {
        set_error(
            error,
            "credential runtime is not loaded"
        );

        return
            StorageNodeCredentialRuntimeResult::
                NotLoaded;
    }

    // Security: execute both checks while holding the same mutex that protects
    // the following mutation. A concurrent finalize must not invalidate an
    // authorization decision between check and use.
    const bool active_match =
        verify_internal_bearer_authorization(
            authorization,
            active_
        );

    const bool pending_match =
        verify_internal_bearer_authorization(
            authorization,
            pending_
        );

    if (!active_match &&
        !pending_match) {
        set_error(
            error,
            "credential rotation authentication failed"
        );

        return
            StorageNodeCredentialRuntimeResult::
                AuthenticationFailed;
    }

    return prepare_rotation_unlocked(
        pending_token,
        error
    );
}

StorageNodeCredentialRuntimeResult
StorageNodeCredentialRuntime::
prepare_rotation_unlocked(
    std::string* pending_token,
    std::string* error
) {
    if (!pending_token) {
        return
            StorageNodeCredentialRuntimeResult::
                InvalidInput;
    }

    clear_internal_auth_token_text(
        pending_token
    );

    if (!loaded_) {
        set_error(
            error,
            "credential runtime is not loaded"
        );

        return
            StorageNodeCredentialRuntimeResult::
                NotLoaded;
    }

    if (pending_unlink_uncertain_) {
        // Security: do not begin another generation while finalization has an
        // unresolved pending-entry durability boundary.
        set_error(
            error,
            "credential rotation finalization is incomplete"
        );

        return
            StorageNodeCredentialRuntimeResult::
                StateMismatch;
    }

    std::string existing_token;
    SensitiveStringWiper
        existing_token_wiper(
            existing_token
        );

    InternalAuthCredential
        existing_candidate;

    const EntryResult existing_result =
        read_credential_at(
            directory_descriptor_,
            pending_filename_,
            &existing_token,
            &existing_candidate,
            error
        );

    if (existing_result ==
        EntryResult::Ok) {
        if (pending_.configured()) {
            if (!token_matches_credential(
                    existing_token,
                    pending_)) {
                set_error(
                    error,
                    "persisted pending credential changed unexpectedly"
                );

                return
                    StorageNodeCredentialRuntimeResult::
                        StorageUnsafe;
            }

            const bool matches_active =
                token_matches_credential(
                    existing_token,
                    active_
                );

            if (matches_active !=
                pending_matches_active_) {
                set_error(
                    error,
                    "pending credential runtime state mismatch"
                );

                return
                    StorageNodeCredentialRuntimeResult::
                        StateMismatch;
            }
        } else {
            const auto sync_result =
                sync_existing_entry(
                    directory_descriptor_,
                    pending_filename_,
                    error
                );

            if (sync_result !=
                StorageNodeCredentialRuntimeResult::
                    Ok) {
                return sync_result;
            }

            pending_matches_active_ =
                token_matches_credential(
                    existing_token,
                    active_
                );

            pending_ =
                std::move(
                    existing_candidate
                );
        }

        *pending_token =
            std::move(
                existing_token
            );

        return
            StorageNodeCredentialRuntimeResult::
                Ok;
    }

    if (existing_result !=
        EntryResult::Missing) {
        return
            map_entry_result(
                existing_result
            );
    }

    if (pending_.configured()) {
        // Security: in-memory authorization must never outlive disappearance
        // of its persisted recovery anchor.
        set_error(
            error,
            "pending credential disappeared unexpectedly"
        );

        return
            StorageNodeCredentialRuntimeResult::
                StateMismatch;
    }

    std::string generated;
    SensitiveStringWiper
        generated_wiper(
            generated
        );

    bool distinct = false;

    for (int attempt = 0;
         attempt < 4;
         ++attempt) {
        if (!generate_internal_auth_credential_token(
                &generated,
                error)) {
            return
                StorageNodeCredentialRuntimeResult::
                    CredentialInvalid;
        }

        if (!token_matches_credential(
                generated,
                active_)) {
            distinct = true;
            break;
        }
    }

    if (!distinct) {
        set_error(
            error,
            "failed to generate a distinct rotation credential"
        );

        return
            StorageNodeCredentialRuntimeResult::
                CredentialInvalid;
    }

    InternalAuthCredential
        pending_candidate;

    if (!parse_candidate(
            generated,
            &pending_candidate,
            error)) {
        return
            StorageNodeCredentialRuntimeResult::
                CredentialInvalid;
    }

    const auto persist_result =
        create_credential_entry(
            directory_descriptor_,
            pending_filename_,
            generated,
            error
        );

    if (persist_result !=
        StorageNodeCredentialRuntimeResult::
            Ok) {
        return persist_result;
    }

    // Security: make NEW acceptable only after pending file + directory entry
    // are both durable.
    pending_ =
        std::move(
            pending_candidate
        );

    pending_matches_active_ = false;
    pending_unlink_uncertain_ = false;

    *pending_token =
        std::move(
            generated
        );

    return
        StorageNodeCredentialRuntimeResult::
            Ok;
}

StorageNodeCredentialRuntimeResult
StorageNodeCredentialRuntime::
finalize_rotation_authorized(
    const std::string& authorization,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::lock_guard<std::mutex>
        lock(mutex_);

    if (!loaded_) {
        set_error(
            error,
            "credential runtime is not loaded"
        );

        return
            StorageNodeCredentialRuntimeResult::
                NotLoaded;
    }

    // Security: finalization is a credential-retirement operation.
    //
    // While rotation is pending, require proof of NEW specifically. OLD may
    // continue authenticating ordinary requests during the overlap window, but
    // it can never retire itself.
    //
    // With no pending rotation, ACTIVE may authenticate an idempotent retry so
    // a lost successful finalize response does not create ambiguous state.
    const bool active_match =
        verify_internal_bearer_authorization(
            authorization,
            active_
        );

    const bool pending_match =
        verify_internal_bearer_authorization(
            authorization,
            pending_
        );

    const bool authorized =
        pending_.configured()
            ? pending_match
            : active_match;

    if (!authorized) {
        set_error(
            error,
            "credential rotation authentication failed"
        );

        return
            StorageNodeCredentialRuntimeResult::
                AuthenticationFailed;
    }

    return finalize_rotation_unlocked(
        error
    );
}

StorageNodeCredentialRuntimeResult
StorageNodeCredentialRuntime::
finalize_rotation_unlocked(
    std::string* error
) {
    if (!loaded_) {
        set_error(
            error,
            "credential runtime is not loaded"
        );

        return
            StorageNodeCredentialRuntimeResult::
                NotLoaded;
    }

    if (!pending_.configured()) {
        return
            StorageNodeCredentialRuntimeResult::
                NoPending;
    }

    const auto cleanup_result =
        cleanup_temporary_entry(
            directory_descriptor_,
            temporary_filename_,
            error
        );

    if (cleanup_result !=
        StorageNodeCredentialRuntimeResult::
            Ok) {
        return cleanup_result;
    }

    std::string pending_token;
    SensitiveStringWiper
        pending_token_wiper(
            pending_token
        );

    InternalAuthCredential
        disk_pending_candidate;

    const EntryResult pending_result =
        read_credential_at(
            directory_descriptor_,
            pending_filename_,
            &pending_token,
            &disk_pending_candidate,
            error
        );

    if (pending_result ==
        EntryResult::Missing) {
        if (pending_unlink_uncertain_ &&
            pending_matches_active_) {
            // Security: a previous unlink succeeded but its directory fsync
            // failed. A successful retry fsync makes absence authoritative.
            if (::fsync(
                    directory_descriptor_) != 0) {
                set_error(
                    error,
                    "failed to confirm pending credential removal"
                );

                return
                    StorageNodeCredentialRuntimeResult::
                        PersistFailed;
            }

            pending_.clear();
            pending_matches_active_ = false;
            pending_unlink_uncertain_ = false;

            return
                StorageNodeCredentialRuntimeResult::
                    Ok;
        }

        set_error(
            error,
            "pending credential disappeared unexpectedly"
        );

        return
            StorageNodeCredentialRuntimeResult::
                StorageUnsafe;
    }

    if (pending_result !=
        EntryResult::Ok) {
        return
            map_entry_result(
                pending_result
            );
    }

    pending_unlink_uncertain_ = false;

    if (!token_matches_credential(
            pending_token,
            pending_)) {
        set_error(
            error,
            "persisted pending credential changed unexpectedly"
        );

        return
            StorageNodeCredentialRuntimeResult::
                StorageUnsafe;
    }

    std::string active_token;
    SensitiveStringWiper
        active_token_wiper(
            active_token
        );

    InternalAuthCredential
        disk_active_candidate;

    const EntryResult active_result =
        read_credential_at(
            directory_descriptor_,
            active_filename_,
            &active_token,
            &disk_active_candidate,
            error
        );

    if (active_result !=
        EntryResult::Ok) {
        set_error(
            error,
            "active credential is unavailable during rotation"
        );

        return
            active_result ==
                EntryResult::Missing
                ? StorageNodeCredentialRuntimeResult::
                      StorageUnsafe
                : map_entry_result(
                      active_result
                  );
    }

    const bool active_matches_current =
        token_matches_credential(
            active_token,
            active_
        );

    const bool active_matches_pending =
        token_matches_credential(
            active_token,
            pending_
        );

    if (pending_matches_active_) {
        if (!active_matches_current ||
            !active_matches_pending) {
            set_error(
                error,
                "durable active credential changed unexpectedly"
            );

            return
                StorageNodeCredentialRuntimeResult::
                    StorageUnsafe;
        }
    } else if (
        active_matches_current &&
        !active_matches_pending) {
        InternalAuthCredential
            promoted_candidate;

        if (!parse_candidate(
                pending_token,
                &promoted_candidate,
                error)) {
            return
                StorageNodeCredentialRuntimeResult::
                    CredentialInvalid;
        }

        const auto temp_result =
            create_credential_entry(
                directory_descriptor_,
                temporary_filename_,
                pending_token,
                error
            );

        if (temp_result !=
            StorageNodeCredentialRuntimeResult::
                Ok) {
            return temp_result;
        }

        // Security: active replacement uses an atomic same-directory rename.
        // OLD remains accepted in memory until the following parent fsync
        // succeeds.
        if (::renameat(
                directory_descriptor_,
                temporary_filename_.c_str(),
                directory_descriptor_,
                active_filename_.c_str()) != 0) {
            set_error(
                error,
                "failed to replace active storage node credential"
            );

            (void)cleanup_temporary_entry(
                directory_descriptor_,
                temporary_filename_,
                nullptr
            );

            return
                StorageNodeCredentialRuntimeResult::
                    PersistFailed;
        }

        if (::fsync(
                directory_descriptor_) != 0) {
            set_error(
                error,
                "failed to sync active credential replacement"
            );

            // Keep OLD + NEW accepted. On retry the visible active file may
            // contain NEW; that state is recognized below and re-fsynced.
            return
                StorageNodeCredentialRuntimeResult::
                    PersistFailed;
        }

        active_ =
            std::move(
                promoted_candidate
            );

        pending_matches_active_ = true;
    } else if (
        !active_matches_current &&
        active_matches_pending) {
        // Recovery from rename succeeding while its parent fsync previously
        // failed. Re-fsync before dropping OLD from the runtime.
        if (::fsync(
                directory_descriptor_) != 0) {
            set_error(
                error,
                "failed to recover active credential replacement"
            );

            return
                StorageNodeCredentialRuntimeResult::
                    PersistFailed;
        }

        InternalAuthCredential
            promoted_candidate;

        if (!parse_candidate(
                pending_token,
                &promoted_candidate,
                error)) {
            return
                StorageNodeCredentialRuntimeResult::
                    CredentialInvalid;
        }

        active_ =
            std::move(
                promoted_candidate
            );

        pending_matches_active_ = true;
    } else if (
        active_matches_current &&
        active_matches_pending) {
        set_error(
            error,
            "credential rotation state is inconsistent"
        );

        return
            StorageNodeCredentialRuntimeResult::
                StateMismatch;
    } else {
        set_error(
            error,
            "active credential changed unexpectedly"
        );

        return
            StorageNodeCredentialRuntimeResult::
                StorageUnsafe;
    }

    // NEW is now durably active. Only after that boundary may the recovery
    // anchor be removed.
    if (::unlinkat(
            directory_descriptor_,
            pending_filename_.c_str(),
            0) != 0) {
        set_error(
            error,
            errno == ENOENT
                ? "pending credential disappeared during finalization"
                : "failed to remove pending credential"
        );

        return
            errno == ENOENT
                ? StorageNodeCredentialRuntimeResult::
                      StorageUnsafe
                : StorageNodeCredentialRuntimeResult::
                      PersistFailed;
    }

    pending_unlink_uncertain_ = true;

    if (::fsync(
            directory_descriptor_) != 0) {
        set_error(
            error,
            "failed to sync pending credential removal"
        );

        // ACTIVE is already NEW and durable. Keep the in-memory pending copy
        // until a later fsync confirms that the on-disk recovery anchor cannot
        // return after crash.
        return
            StorageNodeCredentialRuntimeResult::
                PersistFailed;
    }

    pending_.clear();

    pending_matches_active_ = false;
    pending_unlink_uncertain_ = false;

    return
        StorageNodeCredentialRuntimeResult::
            Ok;
}

bool
StorageNodeCredentialRuntime::
rotation_pending() const {
    std::lock_guard<std::mutex>
        lock(mutex_);

    return
        loaded_ &&
        pending_.configured();
}

} // namespace pqnas::cluster
