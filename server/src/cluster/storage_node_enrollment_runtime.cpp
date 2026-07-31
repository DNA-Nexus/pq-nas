#include "cluster/storage_node_enrollment_runtime.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_enrollment_token.h"
#include "cluster/node_status.h"
#include "cluster/storage_node_provisioning_store.h"

#include <sodium.h>

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utility>

namespace pqnas::cluster {
namespace {

constexpr std::size_t kMaximumPathBytes = 4096;
constexpr std::size_t kMaximumEnrollmentIdBytes = 128;
constexpr std::size_t kMaximumTokenFileBytes =
    kNodeEnrollmentTokenChars + 1;

constexpr char kProvisioningStateFilename[] =
    "storage_node_provisioning.json";

class UniqueFd {
public:
    explicit UniqueFd(
        int descriptor = -1
    ) noexcept
        : descriptor_(descriptor) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : descriptor_(
              other.release()
          ) {}

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
        const int descriptor =
            descriptor_;

        descriptor_ = -1;

        return descriptor;
    }

    void reset(
        int descriptor = -1
    ) noexcept {
        if (descriptor_ >= 0) {
            (void)::close(
                descriptor_
            );
        }

        descriptor_ = descriptor;
    }

private:
    int descriptor_;
};

class SensitiveTokenWiper {
public:
    explicit SensitiveTokenWiper(
        std::string& token
    ) noexcept
        : token_(token) {}

    ~SensitiveTokenWiper() {
        clear_node_enrollment_token_text(
            &token_
        );
    }

    SensitiveTokenWiper(
        const SensitiveTokenWiper&
    ) = delete;

    SensitiveTokenWiper& operator=(
        const SensitiveTokenWiper&
    ) = delete;

private:
    std::string& token_;
};

template <std::size_t Size>
class SensitiveArrayWiper {
public:
    explicit SensitiveArrayWiper(
        std::array<char, Size>& value
    ) noexcept
        : value_(value) {}

    ~SensitiveArrayWiper() {
        sodium_memzero(
            value_.data(),
            value_.size()
        );
    }

    SensitiveArrayWiper(
        const SensitiveArrayWiper&
    ) = delete;

    SensitiveArrayWiper& operator=(
        const SensitiveArrayWiper&
    ) = delete;

private:
    std::array<char, Size>& value_;
};

struct BootstrapTokenLease {
    UniqueFd directory;

    // Security: retain the original file descriptor until cleanup. Keeping the
    // inode referenced prevents immediate inode reuse from making a replacement
    // file appear identical to the token file that was actually read.
    UniqueFd file;

    std::string filename;

    dev_t device = 0;
    ino_t inode = 0;

    bool configured = false;
};

std::string environment_value(
    const char* name
) {
    const char* value =
        std::getenv(name);

    return value
        ? std::string(value)
        : std::string();
}

bool safe_owner(
    uid_t owner
) noexcept {
    return
        owner == ::geteuid() ||
        owner == 0;
}

bool safe_directory(
    const struct stat& metadata
) noexcept {
    return
        S_ISDIR(metadata.st_mode) &&
        safe_owner(metadata.st_uid) &&
        (metadata.st_mode & 0022) == 0;
}

bool safe_token_file(
    const struct stat& metadata
) noexcept {
    return
        S_ISREG(metadata.st_mode) &&
        safe_owner(metadata.st_uid) &&
        metadata.st_nlink == 1 &&
        (metadata.st_mode & 0077) == 0 &&
        metadata.st_size >= 0 &&
        static_cast<unsigned long long>(
            metadata.st_size
        ) <=
            static_cast<unsigned long long>(
                kMaximumTokenFileBytes
            );
}

bool valid_enrollment_id(
    const std::string& value
) noexcept {
    if (value.empty() ||
        value.size() >
            kMaximumEnrollmentIdBytes) {
        return false;
    }

    for (unsigned char character :
         value) {
        const bool allowed =
            (character >= 'A' &&
             character <= 'Z') ||
            (character >= 'a' &&
             character <= 'z') ||
            (character >= '0' &&
             character <= '9') ||
            character == '-' ||
            character == '_';

        if (!allowed) {
            return false;
        }
    }

    return true;
}

bool parse_generation(
    const std::string& value,
    std::uint64_t* output
) noexcept {
    if (!output ||
        value.empty() ||
        value.size() > 20) {
        return false;
    }

    std::uint64_t parsed = 0;

    for (unsigned char character :
         value) {
        if (character < '0' ||
            character > '9') {
            return false;
        }

        const std::uint64_t digit =
            static_cast<std::uint64_t>(
                character - '0'
            );

        if (parsed >
            (
                std::numeric_limits<
                    std::uint64_t
                >::max() -
                digit
            ) /
                10U) {
            return false;
        }

        parsed =
            parsed * 10U +
            digit;
    }

    if (parsed == 0) {
        return false;
    }

    *output = parsed;

    return true;
}

bool normalize_absolute_file_path(
    const std::string& raw,
    std::filesystem::path* output
) {
    if (!output ||
        raw.empty() ||
        raw.size() >
            kMaximumPathBytes ||
        raw.find('\0') !=
            std::string::npos) {
        return false;
    }

    const std::filesystem::path path(
        raw
    );

    if (!path.is_absolute() ||
        !path.has_root_path()) {
        return false;
    }

    for (const auto& component :
         path) {
        const std::string part =
            component.string();

        if (part == "." ||
            part == "..") {
            return false;
        }
    }

    const std::filesystem::path clean =
        path.lexically_normal();

    if (clean.empty() ||
        clean == clean.root_path() ||
        clean.filename().empty() ||
        clean != path) {
        return false;
    }

    *output = clean;

    return true;
}

bool valid_bootstrap_token(
    const std::string& token
) {
    std::string verifier;

    const NodeEnrollmentTokenResult result =
        hash_node_enrollment_token(
            token,
            &verifier
        );

    clear_node_enrollment_token_text(
        &verifier
    );

    return
        result ==
        NodeEnrollmentTokenResult::Ok;
}

bool read_all_bounded(
    int descriptor,
    std::string* output
) {
    if (!output) {
        return false;
    }

    clear_node_enrollment_token_text(
        output
    );

    std::array<
        char,
        kMaximumTokenFileBytes + 1
    > buffer{};

    SensitiveArrayWiper<
        kMaximumTokenFileBytes + 1
    > buffer_wiper(buffer);

    std::size_t total = 0;

    while (total <
           buffer.size()) {
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

        return false;
    }

    if (total >
        kMaximumTokenFileBytes) {
        return false;
    }

    try {
        output->assign(
            buffer.data(),
            total
        );
    } catch (...) {
        clear_node_enrollment_token_text(
            output
        );

        return false;
    }

    return true;
}

bool load_bootstrap_token(
    const std::string& configured_path,
    std::string* token,
    BootstrapTokenLease* lease
) {
    if (!token ||
        !lease) {
        return false;
    }

    clear_node_enrollment_token_text(
        token
    );

    lease->directory.reset();
    lease->file.reset();
    lease->filename.clear();
    lease->device = 0;
    lease->inode = 0;
    lease->configured = false;

    std::filesystem::path path;

    if (!normalize_absolute_file_path(
            configured_path,
            &path)) {
        return false;
    }

    const std::filesystem::path parent =
        path.parent_path();

    if (parent.empty() ||
        parent == parent.root_path()) {
        return false;
    }

    const int raw_directory =
        ::open(
            parent.c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_directory < 0) {
        return false;
    }

    UniqueFd directory(
        raw_directory
    );

    struct stat directory_metadata{};

    if (::fstat(
            directory.get(),
            &directory_metadata) != 0 ||
        !safe_directory(
            directory_metadata)) {
        return false;
    }

    const std::string filename =
        path.filename().string();

    const int raw_file =
        ::openat(
            directory.get(),
            filename.c_str(),
            O_RDONLY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_file < 0) {
        return false;
    }

    UniqueFd file(
        raw_file
    );

    struct stat file_metadata{};

    if (::fstat(
            file.get(),
            &file_metadata) != 0 ||
        !safe_token_file(
            file_metadata) ||
        !read_all_bounded(
            file.get(),
            token)) {
        clear_node_enrollment_token_text(
            token
        );

        return false;
    }

    if (!token->empty() &&
        token->back() == '\n') {
        token->pop_back();
    }

    if (token->find('\n') !=
            std::string::npos ||
        token->find('\r') !=
            std::string::npos ||
        !valid_bootstrap_token(
            *token)) {
        clear_node_enrollment_token_text(
            token
        );

        return false;
    }

    lease->directory =
        std::move(directory);

    lease->file =
        std::move(file);

    lease->filename =
        filename;

    lease->device =
        file_metadata.st_dev;

    lease->inode =
        file_metadata.st_ino;

    lease->configured = true;

    return true;
}

bool remove_token_if_unchanged(
    const BootstrapTokenLease& lease
) noexcept {
    if (!lease.configured ||
        lease.directory.get() < 0 ||
        lease.file.get() < 0 ||
        lease.filename.empty()) {
        return true;
    }

    struct stat original{};

    if (::fstat(
            lease.file.get(),
            &original) != 0 ||
        !safe_token_file(original) ||
        original.st_dev != lease.device ||
        original.st_ino != lease.inode) {
        return false;
    }

    struct stat current{};

    if (::fstatat(
            lease.directory.get(),
            lease.filename.c_str(),
            &current,
            AT_SYMLINK_NOFOLLOW) != 0 ||
        !safe_token_file(current) ||
        current.st_dev != lease.device ||
        current.st_ino != lease.inode) {
        // Security: never unlink a replacement installed during enrollment.
        return false;
    }

    if (::unlinkat(
            lease.directory.get(),
            lease.filename.c_str(),
            0) != 0) {
        return false;
    }

    // The token is already invalid at the Director, but make removal durable
    // whenever the filesystem supports directory synchronization.
    return
        ::fsync(
            lease.directory.get()
        ) == 0;
}

StorageNodeEnrollmentRuntimeResult finish(
    StorageNodeEnrollmentRuntimeCode code,
    bool recovered = false,
    bool already_enrolled = false,
    bool cleanup_warning = false
) noexcept {
    StorageNodeEnrollmentRuntimeResult result;

    result.code = code;

    result.ok =
        code ==
            StorageNodeEnrollmentRuntimeCode::Ok ||
        code ==
            StorageNodeEnrollmentRuntimeCode::
                Disabled;

    result.recovered =
        code ==
            StorageNodeEnrollmentRuntimeCode::Ok &&
        recovered;

    result.already_enrolled =
        code ==
            StorageNodeEnrollmentRuntimeCode::Ok &&
        already_enrolled;

    result.token_cleanup_warning =
        result.ok &&
        cleanup_warning;

    return result;
}

StorageNodeEnrollmentRuntimeCode
map_client_result(
    StorageNodeEnrollmentClientCode code
) noexcept {
    switch (code) {
        case StorageNodeEnrollmentClientCode::
                Ok:
            return
                StorageNodeEnrollmentRuntimeCode::
                    Ok;

        case StorageNodeEnrollmentClientCode::
                InvalidInput:
        case StorageNodeEnrollmentClientCode::
                ClientUnavailable:
            return
                StorageNodeEnrollmentRuntimeCode::
                    InvalidConfig;

        case StorageNodeEnrollmentClientCode::
                LocalStateUnavailable:
        case StorageNodeEnrollmentClientCode::
                StateConflict:
            return
                StorageNodeEnrollmentRuntimeCode::
                    StateUnavailable;

        case StorageNodeEnrollmentClientCode::
                AuthenticationFailed:
            return
                StorageNodeEnrollmentRuntimeCode::
                    AuthenticationFailed;

        case StorageNodeEnrollmentClientCode::
                DirectorConflict:
            return
                StorageNodeEnrollmentRuntimeCode::
                    DirectorConflict;

        case StorageNodeEnrollmentClientCode::
                RateLimited:
            return
                StorageNodeEnrollmentRuntimeCode::
                    RateLimited;

        case StorageNodeEnrollmentClientCode::
                DirectorUnavailable:
            return
                StorageNodeEnrollmentRuntimeCode::
                    DirectorUnavailable;

        case StorageNodeEnrollmentClientCode::
                ProtocolError:
            return
                StorageNodeEnrollmentRuntimeCode::
                    ProtocolError;

        case StorageNodeEnrollmentClientCode::
                PersistFailed:
            return
                StorageNodeEnrollmentRuntimeCode::
                    PersistFailed;
    }

    return
        StorageNodeEnrollmentRuntimeCode::
            ProtocolError;
}

} // namespace

const char*
storage_node_enrollment_runtime_config_result_code(
    StorageNodeEnrollmentRuntimeConfigResult result
) noexcept {
    switch (result) {
        case StorageNodeEnrollmentRuntimeConfigResult::
                Ok:
            return
                "storage_node_enrollment_runtime_config_ok";

        case StorageNodeEnrollmentRuntimeConfigResult::
                InvalidInput:
            return
                "storage_node_enrollment_runtime_config_invalid";
    }

    return
        "storage_node_enrollment_runtime_config_unknown";
}

StorageNodeEnrollmentRuntimeConfigResult
parse_storage_node_enrollment_runtime_config(
    const std::string& raw_enrollment_id,
    const std::string& raw_generation,
    const std::string& raw_api_base_url,
    const std::string& raw_bootstrap_token_file,
    StorageNodeEnrollmentRuntimeConfig* output
) {
    if (!output) {
        return
            StorageNodeEnrollmentRuntimeConfigResult::
                InvalidInput;
    }

    *output =
        StorageNodeEnrollmentRuntimeConfig{};

    const bool any_identity =
        !raw_enrollment_id.empty() ||
        !raw_generation.empty() ||
        !raw_api_base_url.empty();

    const bool token_path_present =
        !raw_bootstrap_token_file.empty();

    if (!any_identity &&
        !token_path_present) {
        return
            StorageNodeEnrollmentRuntimeConfigResult::
                Ok;
    }

    if (!any_identity ||
        raw_enrollment_id.empty() ||
        raw_generation.empty() ||
        raw_api_base_url.empty()) {
        return
            StorageNodeEnrollmentRuntimeConfigResult::
                InvalidInput;
    }

    std::uint64_t generation = 0;
    std::string api_base_url;

    if (!valid_enrollment_id(
            raw_enrollment_id) ||
        !parse_generation(
            raw_generation,
            &generation) ||
        !normalize_cluster_https_base_url(
            raw_api_base_url,
            &api_base_url,
            nullptr)) {
        return
            StorageNodeEnrollmentRuntimeConfigResult::
                InvalidInput;
    }

    std::string token_file;

    if (token_path_present) {
        std::filesystem::path normalized;

        if (!normalize_absolute_file_path(
                raw_bootstrap_token_file,
                &normalized)) {
            return
                StorageNodeEnrollmentRuntimeConfigResult::
                    InvalidInput;
        }

        token_file =
            normalized.string();
    }

    output->enabled = true;

    output->enrollment_id =
        raw_enrollment_id;

    output->expected_generation =
        generation;

    output->api_base_url =
        std::move(api_base_url);

    output->bootstrap_token_file =
        std::move(token_file);

    return
        StorageNodeEnrollmentRuntimeConfigResult::
            Ok;
}

StorageNodeEnrollmentRuntimeConfigResult
load_storage_node_enrollment_runtime_config_from_env(
    StorageNodeEnrollmentRuntimeConfig* output
) {
    return
        parse_storage_node_enrollment_runtime_config(
            environment_value(
                "PQNAS_NODE_ENROLLMENT_ID"
            ),
            environment_value(
                "PQNAS_NODE_ENROLLMENT_GENERATION"
            ),
            environment_value(
                "PQNAS_NODE_API_BASE_URL"
            ),
            environment_value(
                "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE"
            ),
            output
        );
}

const char* storage_node_enrollment_runtime_code(
    StorageNodeEnrollmentRuntimeCode code
) noexcept {
    switch (code) {
        case StorageNodeEnrollmentRuntimeCode::
                Disabled:
            return
                "storage_node_enrollment_disabled";

        case StorageNodeEnrollmentRuntimeCode::
                Ok:
            return
                "storage_node_enrollment_ready";

        case StorageNodeEnrollmentRuntimeCode::
                InvalidRole:
            return
                "storage_node_enrollment_invalid_role";

        case StorageNodeEnrollmentRuntimeCode::
                InvalidConfig:
            return
                "storage_node_enrollment_invalid_config";

        case StorageNodeEnrollmentRuntimeCode::
                StateUnavailable:
            return
                "storage_node_enrollment_state_unavailable";

        case StorageNodeEnrollmentRuntimeCode::
                BootstrapTokenUnavailable:
            return
                "storage_node_enrollment_bootstrap_token_unavailable";

        case StorageNodeEnrollmentRuntimeCode::
                AuthenticationFailed:
            return
                "storage_node_enrollment_authentication_failed";

        case StorageNodeEnrollmentRuntimeCode::
                DirectorConflict:
            return
                "storage_node_enrollment_director_conflict";

        case StorageNodeEnrollmentRuntimeCode::
                RateLimited:
            return
                "storage_node_enrollment_rate_limited";

        case StorageNodeEnrollmentRuntimeCode::
                DirectorUnavailable:
            return
                "storage_node_enrollment_director_unavailable";

        case StorageNodeEnrollmentRuntimeCode::
                ProtocolError:
            return
                "storage_node_enrollment_protocol_error";

        case StorageNodeEnrollmentRuntimeCode::
                PersistFailed:
            return
                "storage_node_enrollment_persist_failed";
    }

    return
        "storage_node_enrollment_runtime_unknown";
}

const char*
StorageNodeEnrollmentRuntimeResult::error_code()
    const noexcept {
    return storage_node_enrollment_runtime_code(
        code
    );
}

StorageNodeEnrollmentRuntimeResult
ensure_storage_node_enrollment(
    const ServerRoleConfig& role_config,
    const StorageNodeEnrollmentRuntimeConfig&
        enrollment_config,
    StorageNodeEnrollmentRuntimeDependencies
        dependencies
) {
    if (!enrollment_config.enabled) {
        return finish(
            StorageNodeEnrollmentRuntimeCode::
                Disabled
        );
    }

    if (!role_config.is_node() ||
        !valid_cluster_node_id(
            role_config.node_id) ||
        role_config.director_url.empty() ||
        role_config.cluster_credential_file.empty()) {
        return finish(
            StorageNodeEnrollmentRuntimeCode::
                InvalidRole
        );
    }

    if (!dependencies.transport ||
        !dependencies.now_iso_utc ||
        !valid_enrollment_id(
            enrollment_config.enrollment_id) ||
        enrollment_config.expected_generation == 0 ||
        enrollment_config.api_base_url.empty()) {
        return finish(
            StorageNodeEnrollmentRuntimeCode::
                InvalidConfig
        );
    }

    const std::filesystem::path credential_path(
        role_config.cluster_credential_file
    );

    const std::filesystem::path state_path =
        credential_path.parent_path() /
        kProvisioningStateFilename;

    if (credential_path.filename() ==
        state_path.filename()) {
        return finish(
            StorageNodeEnrollmentRuntimeCode::
                InvalidConfig
        );
    }

    StorageNodeProvisioningStore store;
    std::string internal_error;

    if (!store.load(
            state_path.string(),
            credential_path.string(),
            &internal_error)) {
        internal_error.clear();

        return finish(
            StorageNodeEnrollmentRuntimeCode::
                StateUnavailable
        );
    }

    std::string bootstrap_token;
    SensitiveTokenWiper token_wiper(
        bootstrap_token
    );

    BootstrapTokenLease lease;

    if (!enrollment_config
             .bootstrap_token_file
             .empty() &&
        !load_bootstrap_token(
            enrollment_config
                .bootstrap_token_file,
            &bootstrap_token,
            &lease)) {
        return finish(
            StorageNodeEnrollmentRuntimeCode::
                BootstrapTokenUnavailable
        );
    }

    StorageNodeProvisioningIntent intent;

    intent.enrollment_id =
        enrollment_config.enrollment_id;

    intent.expected_generation =
        enrollment_config
            .expected_generation;

    intent.node_id =
        role_config.node_id;

    intent.director_url =
        role_config.director_url;

    intent.api_base_url =
        enrollment_config.api_base_url;

    intent.protocol_version =
        kNodeStatusProtocolVersion;

    StorageNodeEnrollmentClientConfig
        client_config;

    client_config.store =
        &store;

    client_config.transport =
        std::move(
            dependencies.transport
        );

    client_config.now_iso_utc =
        std::move(
            dependencies.now_iso_utc
        );

    StorageNodeEnrollmentClient client(
        std::move(client_config)
    );

    const StorageNodeEnrollmentClientResult
        client_result =
            client.enroll(
                intent,
                bootstrap_token
            );

    if (!client_result.ok) {
        return finish(
            map_client_result(
                client_result.code
            )
        );
    }

    const bool cleanup_warning =
        lease.configured &&
        !remove_token_if_unchanged(
            lease
        );

    return finish(
        StorageNodeEnrollmentRuntimeCode::Ok,
        client_result.recovered,
        client_result.already_enrolled,
        cleanup_warning
    );
}

} // namespace pqnas::cluster
