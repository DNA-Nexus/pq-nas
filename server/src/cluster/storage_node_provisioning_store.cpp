#include "cluster/storage_node_provisioning_store.h"

#include "cluster/cluster_identity_validation.h"
#include "cluster/internal_auth.h"
#include "cluster/node_tls_identity.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

// Schema v2 adds the Director TLS SPKI identity to the durable
// enrollment transaction. v1 remains parseable only for upgrade handling.
constexpr std::size_t kMaximumPathBytes = 4096;
constexpr std::size_t kMaximumFilenameBytes = 255;
constexpr std::size_t kMaximumStateFileBytes = 16 * 1024;
constexpr std::size_t kMaximumEnrollmentIdBytes = 128;
constexpr std::size_t kMaximumTimestampBytes = 20;
constexpr std::size_t kMaximumCredentialFileBytes =
    kInternalAuthTokenChars + 1;

std::atomic<std::uint64_t> g_temporary_counter{0};

class UniqueFd {
public:
    explicit UniqueFd(int descriptor = -1) noexcept
        : descriptor_(descriptor) {}

    ~UniqueFd() {
        reset();
    }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept
        : descriptor_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
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

    void reset(int descriptor = -1) noexcept {
        if (descriptor_ >= 0) {
            (void)::close(descriptor_);
        }

        descriptor_ = descriptor;
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

enum class FileInspectResult {
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

bool digit(
    char value
) noexcept {
    return value >= '0' &&
           value <= '9';
}

bool leap_year(
    int year
) noexcept {
    return
        year % 400 == 0 ||
        (
            year % 4 == 0 &&
            year % 100 != 0
        );
}

int days_in_month(
    int year,
    int month
) noexcept {
    static constexpr int days[] = {
        31,
        28,
        31,
        30,
        31,
        30,
        31,
        31,
        30,
        31,
        30,
        31
    };

    if (month < 1 ||
        month > 12) {
        return 0;
    }

    if (month == 2 &&
        leap_year(year)) {
        return 29;
    }

    return days[month - 1];
}

int decimal_component(
    const std::string& value,
    std::size_t offset,
    std::size_t count
) noexcept {
    int result = 0;

    for (std::size_t index = 0;
         index < count;
         ++index) {
        const char character =
            value[offset + index];

        if (!digit(character)) {
            return -1;
        }

        result =
            result * 10 +
            (character - '0');
    }

    return result;
}

bool valid_iso_utc_seconds_text(
    const std::string& value
) noexcept {
    if (value.size() !=
        kMaximumTimestampBytes ||
        value[4] != '-' ||
        value[7] != '-' ||
        value[10] != 'T' ||
        value[13] != ':' ||
        value[16] != ':' ||
        value[19] != 'Z') {
        return false;
    }

    const int year =
        decimal_component(
            value,
            0,
            4
        );

    const int month =
        decimal_component(
            value,
            5,
            2
        );

    const int day =
        decimal_component(
            value,
            8,
            2
        );

    const int hour =
        decimal_component(
            value,
            11,
            2
        );

    const int minute =
        decimal_component(
            value,
            14,
            2
        );

    const int second =
        decimal_component(
            value,
            17,
            2
        );

    return
        year >= 1970 &&
        month >= 1 &&
        month <= 12 &&
        day >= 1 &&
        day <= days_in_month(
            year,
            month
        ) &&
        hour >= 0 &&
        hour <= 23 &&
        minute >= 0 &&
        minute <= 59 &&
        second >= 0 &&
        second <= 59;
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

bool normalize_intent_impl(
    const StorageNodeProvisioningIntent&
        input,
    bool require_director_tls_pin,
    StorageNodeProvisioningIntent* output,
    std::string* error
) {
    if (!output ||
        !valid_enrollment_id(
            input.enrollment_id) ||
        input.expected_generation == 0 ||
        !valid_cluster_node_id(
            input.node_id) ||
        input.protocol_version < 1 ||
        input.protocol_version > 1000) {
        set_error(
            error,
            "invalid storage node provisioning intent"
        );

        return false;
    }

    std::string director_url;
    std::string director_tls_pin;
    std::string api_base_url;

    if (!normalize_cluster_https_base_url(
            input.director_url,
            &director_url,
            nullptr) ||
        !normalize_cluster_https_base_url(
            input.api_base_url,
            &api_base_url,
            nullptr)) {
        set_error(
            error,
            "invalid storage node provisioning URL"
        );

        return false;
    }

    if (require_director_tls_pin) {
        if (!normalize_node_tls_spki_sha256_pin(
                input.director_tls_spki_sha256_pin,
                &director_tls_pin,
                nullptr) ||
            director_tls_pin !=
                input.director_tls_spki_sha256_pin) {
            set_error(
                error,
                "invalid Director TLS SPKI pin"
            );

            return false;
        }
    } else if (!input.director_tls_spki_sha256_pin.empty()) {
        set_error(
            error,
            "legacy provisioning state must not contain Director TLS pin"
        );

        return false;
    }

    output->enrollment_id =
        input.enrollment_id;

    output->expected_generation =
        input.expected_generation;

    output->node_id =
        input.node_id;

    output->director_url =
        std::move(director_url);

    output->director_tls_spki_sha256_pin =
        std::move(director_tls_pin);

    output->api_base_url =
        std::move(api_base_url);

    output->protocol_version =
        input.protocol_version;

    return true;
}

bool normalize_intent(
    const StorageNodeProvisioningIntent&
        input,
    StorageNodeProvisioningIntent* output,
    std::string* error
) {
    return normalize_intent_impl(
        input,
        true,
        output,
        error
    );
}

bool normalize_legacy_intent(
    const StorageNodeProvisioningIntent&
        input,
    StorageNodeProvisioningIntent* output
) {
    return normalize_intent_impl(
        input,
        false,
        output,
        nullptr
    );
}

bool same_intent(
    const StorageNodeProvisioningRecord&
        record,
    const StorageNodeProvisioningIntent&
        intent
) noexcept {
    return
        record.enrollment_id ==
            intent.enrollment_id &&
        record.expected_generation ==
            intent.expected_generation &&
        record.node_id ==
            intent.node_id &&
        record.director_url ==
            intent.director_url &&
        record.director_tls_spki_sha256_pin ==
            intent.director_tls_spki_sha256_pin &&
        record.api_base_url ==
            intent.api_base_url &&
        record.protocol_version ==
            intent.protocol_version;
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

bool valid_absolute_file_path(
    const std::filesystem::path& path,
    std::string* error
) {
    const std::string text =
        path.string();

    if (text.empty() ||
        text.size() >
            kMaximumPathBytes ||
        !path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            error,
            "provisioning paths must be bounded absolute paths"
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
                "provisioning paths must not contain dot components"
            );

            return false;
        }
    }

    const std::filesystem::path clean =
        path.lexically_normal();

    if (clean.empty() ||
        clean == clean.root_path() ||
        clean.filename().empty() ||
        clean.filename().string().size() >
            kMaximumFilenameBytes) {
        set_error(
            error,
            "provisioning path must name a bounded file"
        );

        return false;
    }

    return clean == path;
}

bool normalize_store_paths(
    const std::string& state_file,
    const std::string& credential_file,
    std::filesystem::path* directory,
    std::string* state_filename,
    std::string* credential_filename,
    std::string* error
) {
    if (!directory ||
        !state_filename ||
        !credential_filename) {
        set_error(
            error,
            "missing provisioning path output"
        );

        return false;
    }

    const std::filesystem::path state_path(
        state_file
    );

    const std::filesystem::path credential_path(
        credential_file
    );

    if (!valid_absolute_file_path(
            state_path,
            error) ||
        !valid_absolute_file_path(
            credential_path,
            error)) {
        return false;
    }

    const std::filesystem::path
        state_directory =
            state_path.parent_path();

    const std::filesystem::path
        credential_directory =
            credential_path.parent_path();

    if (state_directory.empty() ||
        state_directory ==
            state_directory.root_path() ||
        state_directory !=
            credential_directory ||
        state_path.filename() ==
            credential_path.filename()) {
        set_error(
            error,
            "provisioning files must be distinct siblings below a private directory"
        );

        return false;
    }

    *directory =
        state_directory;

    *state_filename =
        state_path.filename().string();

    *credential_filename =
        credential_path.filename().string();

    return true;
}

bool ensure_private_directory(
    const std::filesystem::path& path,
    UniqueFd* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing provisioning directory output"
        );

        return false;
    }

    struct stat path_stats {};

    if (::lstat(
            path.c_str(),
            &path_stats) != 0) {
        if (errno != ENOENT) {
            set_error(
                error,
                "failed to inspect provisioning directory"
            );

            return false;
        }

        if (::mkdir(
                path.c_str(),
                0700) != 0 &&
            errno != EEXIST) {
            set_error(
                error,
                "failed to create provisioning directory"
            );

            return false;
        }
    } else if (!S_ISDIR(
                   path_stats.st_mode) ||
               !safe_directory_metadata(
                   path_stats)) {
        set_error(
            error,
            "unsafe provisioning directory"
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
            "failed to open provisioning directory"
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
            "unsafe opened provisioning directory"
        );

        return false;
    }

    *output =
        std::move(descriptor);

    return true;
}

FileInspectResult inspect_file(
    int directory_descriptor,
    const std::string& filename,
    std::size_t maximum_size,
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
            return FileInspectResult::Missing;
        }

        set_error(
            error,
            "failed to inspect provisioning file"
        );

        return FileInspectResult::IoFailed;
    }

    if (S_ISLNK(stats.st_mode) ||
        !S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        !safe_owner(stats.st_uid) ||
        (stats.st_mode & 0077) != 0) {
        set_error(
            error,
            "unsafe provisioning file"
        );

        return FileInspectResult::Unsafe;
    }

    if (!safe_regular_metadata(
            stats,
            maximum_size)) {
        set_error(
            error,
            "invalid provisioning file size"
        );

        return FileInspectResult::Invalid;
    }

    if (exists) {
        *exists = true;
    }

    return FileInspectResult::Ok;
}

bool read_file_bounded(
    int directory_descriptor,
    const std::string& filename,
    std::size_t maximum_size,
    std::string* output,
    bool sensitive,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing provisioning file output"
        );

        return false;
    }

    if (sensitive) {
        clear_internal_auth_token_text(
            output
        );
    } else {
        output->clear();
    }

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
            "failed to open provisioning file"
        );

        return false;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    struct stat stats {};

    if (::fstat(
            descriptor.get(),
            &stats) != 0 ||
        !safe_regular_metadata(
            stats,
            maximum_size)) {
        set_error(
            error,
            "unsafe opened provisioning file"
        );

        return false;
    }

    try {
        output->assign(
            maximum_size + 1,
            '\0'
        );
    } catch (...) {
        if (sensitive) {
            clear_internal_auth_token_text(
                output
            );
        } else {
            output->clear();
        }

        set_error(
            error,
            "failed to allocate provisioning file buffer"
        );

        return false;
    }

    std::size_t total = 0;

    while (total <
           output->size()) {
        const ssize_t count =
            ::read(
                descriptor.get(),
                output->data() + total,
                output->size() - total
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

        if (sensitive) {
            clear_internal_auth_token_text(
                output
            );
        } else {
            output->clear();
        }

        set_error(
            error,
            "failed to read provisioning file"
        );

        return false;
    }

    if (total > maximum_size) {
        if (sensitive) {
            clear_internal_auth_token_text(
                output
            );
        } else {
            output->clear();
        }

        set_error(
            error,
            "provisioning file is too large"
        );

        return false;
    }

    output->resize(total);

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
            "failed to write provisioning file"
        );

        return false;
    }

    return true;
}

std::string temporary_filename(
    const std::string& target
) {
    const std::uint64_t counter =
        g_temporary_counter.fetch_add(
            1
        );

    return
        "." +
        target +
        ".tmp." +
        std::to_string(
            static_cast<unsigned long long>(
                ::getpid()
            )
        ) +
        "." +
        std::to_string(
            static_cast<unsigned long long>(
                counter
            )
        );
}

StorageNodeProvisioningStoreResult
map_file_result(
    FileInspectResult result
) noexcept {
    switch (result) {
        case FileInspectResult::Ok:
            return
                StorageNodeProvisioningStoreResult::
                    Ok;

        case FileInspectResult::Missing:
            return
                StorageNodeProvisioningStoreResult::
                    CredentialMissing;

        case FileInspectResult::Unsafe:
            return
                StorageNodeProvisioningStoreResult::
                    StorageUnsafe;

        case FileInspectResult::Invalid:
            return
                StorageNodeProvisioningStoreResult::
                    CredentialInvalid;

        case FileInspectResult::IoFailed:
            return
                StorageNodeProvisioningStoreResult::
                    IoFailed;
    }

    return
        StorageNodeProvisioningStoreResult::
            IoFailed;
}

const char* stage_text(
    StorageNodeProvisioningStage stage
) noexcept {
    switch (stage) {
        case StorageNodeProvisioningStage::
                Prepared:
            return "prepared";

        case StorageNodeProvisioningStage::
                CredentialPersisted:
            return "credential_persisted";

        case StorageNodeProvisioningStage::
                Enrolled:
            return "enrolled";
    }

    return "unknown";
}

bool parse_stage(
    const std::string& value,
    StorageNodeProvisioningStage* output
) noexcept {
    if (!output) {
        return false;
    }

    if (value == "prepared") {
        *output =
            StorageNodeProvisioningStage::
                Prepared;

        return true;
    }

    if (value ==
        "credential_persisted") {
        *output =
            StorageNodeProvisioningStage::
                CredentialPersisted;

        return true;
    }

    if (value == "enrolled") {
        *output =
            StorageNodeProvisioningStage::
                Enrolled;

        return true;
    }

    return false;
}

json record_json(
    const StorageNodeProvisioningRecord&
        record
) {
    json object{
        {"version", record.version},
        {
            "stage",
            stage_text(record.stage)
        },
        {
            "enrollment_id",
            record.enrollment_id
        },
        {
            "expected_generation",
            record.expected_generation
        },
        {"node_id", record.node_id},
        {
            "director_url",
            record.director_url
        },
        {
            "api_base_url",
            record.api_base_url
        },
        {
            "protocol_version",
            record.protocol_version
        },
        {
            "prepared_at",
            record.prepared_at
        },
        {
            "updated_at",
            record.updated_at
        },
        {
            "enrolled_at",
            record.enrolled_at
        }
    };

    if (record.version ==
        kStorageNodeProvisioningStateVersionCurrent) {
        object[
            "director_tls_spki_sha256_pin"
        ] =
            record.director_tls_spki_sha256_pin;
    }

    return object;
}

bool has_exact_record_keys(
    const json& object,
    int version
) {
    static constexpr const char*
        common_keys[] = {
            "version",
            "stage",
            "enrollment_id",
            "expected_generation",
            "node_id",
            "director_url",
            "api_base_url",
            "protocol_version",
            "prepared_at",
            "updated_at",
            "enrolled_at"
        };

    if (version !=
            kStorageNodeProvisioningStateVersionLegacy &&
        version !=
            kStorageNodeProvisioningStateVersionCurrent) {
        return false;
    }

    const std::size_t expected_size =
        sizeof(common_keys) /
            sizeof(common_keys[0]) +
        (
            version ==
                kStorageNodeProvisioningStateVersionCurrent
            ? 1U
            : 0U
        );

    if (!object.is_object() ||
        object.size() !=
            expected_size) {
        return false;
    }

    for (const char* key :
         common_keys) {
        if (object.find(key) ==
            object.end()) {
            return false;
        }
    }

    if (version ==
        kStorageNodeProvisioningStateVersionCurrent) {
        return
            object.find(
                "director_tls_spki_sha256_pin"
            ) != object.end();
    }

    return true;
}

bool parse_record(
    const std::string& text,
    StorageNodeProvisioningRecord* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing provisioning record output"
        );

        return false;
    }

    json object;

    try {
        object =
            json::parse(text);
    } catch (...) {
        set_error(
            error,
            "invalid provisioning state JSON"
        );

        return false;
    }

    if (!object.is_object() ||
        object.find("version") ==
            object.end() ||
        !object["version"].
            is_number_integer()) {
        set_error(
            error,
            "invalid provisioning state schema"
        );

        return false;
    }

    int schema_version = 0;

    try {
        schema_version =
            object["version"].get<int>();
    } catch (...) {
        set_error(
            error,
            "invalid provisioning state schema"
        );

        return false;
    }

    if (!has_exact_record_keys(
            object,
            schema_version)) {
        set_error(
            error,
            "invalid provisioning state schema"
        );

        return false;
    }

    StorageNodeProvisioningRecord candidate;
    candidate.version =
        schema_version;

    std::string stage;

    try {
        if (!object["stage"].is_string() ||
            !object["enrollment_id"].
                is_string() ||
            !object["expected_generation"].
                is_number_integer() ||
            !object["node_id"].is_string() ||
            !object["director_url"].
                is_string() ||
            (
                schema_version ==
                    kStorageNodeProvisioningStateVersionCurrent &&
                !object[
                    "director_tls_spki_sha256_pin"
                ].is_string()
            ) ||
            !object["api_base_url"].
                is_string() ||
            !object["protocol_version"].
                is_number_integer() ||
            !object["prepared_at"].
                is_string() ||
            !object["updated_at"].
                is_string() ||
            !object["enrolled_at"].
                is_string()) {
            set_error(
                error,
                "invalid provisioning state field type"
            );

            return false;
        }

        stage =
            object["stage"].
                get<std::string>();

        candidate.enrollment_id =
            object["enrollment_id"].
                get<std::string>();

        if (object["expected_generation"].
                is_number_unsigned()) {
            candidate.expected_generation =
                object["expected_generation"].
                    get<std::uint64_t>();
        } else {
            const std::int64_t generation =
                object["expected_generation"].
                    get<std::int64_t>();

            if (generation <= 0) {
                set_error(
                    error,
                    "invalid provisioning generation"
                );

                return false;
            }

            candidate.expected_generation =
                static_cast<std::uint64_t>(
                    generation
                );
        }

        candidate.node_id =
            object["node_id"].
                get<std::string>();

        candidate.director_url =
            object["director_url"].
                get<std::string>();

        if (schema_version ==
            kStorageNodeProvisioningStateVersionCurrent) {
            candidate.director_tls_spki_sha256_pin =
                object[
                    "director_tls_spki_sha256_pin"
                ].get<std::string>();
        }

        candidate.api_base_url =
            object["api_base_url"].
                get<std::string>();

        candidate.protocol_version =
            object["protocol_version"].
                get<int>();

        candidate.prepared_at =
            object["prepared_at"].
                get<std::string>();

        candidate.updated_at =
            object["updated_at"].
                get<std::string>();

        candidate.enrolled_at =
            object["enrolled_at"].
                get<std::string>();
    } catch (...) {
        set_error(
            error,
            "invalid provisioning state value"
        );

        return false;
    }

    StorageNodeProvisioningIntent
        raw_intent;

    raw_intent.enrollment_id =
        candidate.enrollment_id;

    raw_intent.expected_generation =
        candidate.expected_generation;

    raw_intent.node_id =
        candidate.node_id;

    raw_intent.director_url =
        candidate.director_url;

    raw_intent.director_tls_spki_sha256_pin =
        candidate.director_tls_spki_sha256_pin;

    raw_intent.api_base_url =
        candidate.api_base_url;

    raw_intent.protocol_version =
        candidate.protocol_version;

    StorageNodeProvisioningIntent
        normalized_intent;

    const bool intent_valid =
        candidate.version ==
            kStorageNodeProvisioningStateVersionCurrent
        ? normalize_intent(
              raw_intent,
              &normalized_intent,
              nullptr
          )
        : normalize_legacy_intent(
              raw_intent,
              &normalized_intent
          );

    if (!parse_stage(
            stage,
            &candidate.stage) ||
        !intent_valid ||
        normalized_intent.director_url !=
            candidate.director_url ||
        normalized_intent.director_tls_spki_sha256_pin !=
            candidate.director_tls_spki_sha256_pin ||
        normalized_intent.api_base_url !=
            candidate.api_base_url ||
        !valid_iso_utc_seconds_text(
            candidate.prepared_at) ||
        !valid_iso_utc_seconds_text(
            candidate.updated_at) ||
        candidate.updated_at <
            candidate.prepared_at) {
        set_error(
            error,
            "invalid provisioning state content"
        );

        return false;
    }

    if (candidate.stage ==
            StorageNodeProvisioningStage::
                Enrolled) {
        if (!valid_iso_utc_seconds_text(
                candidate.enrolled_at) ||
            candidate.enrolled_at <
                candidate.prepared_at ||
            candidate.updated_at !=
                candidate.enrolled_at) {
            set_error(
                error,
                "invalid enrolled provisioning timestamps"
            );

            return false;
        }
    } else if (!candidate.enrolled_at.empty()) {
        set_error(
            error,
            "unenrolled provisioning state must not have enrolled_at"
        );

        return false;
    }

    *output =
        std::move(candidate);

    return true;
}

StorageNodeProvisioningStoreResult
commit_record(
    int directory_descriptor,
    const std::string& state_filename,
    const StorageNodeProvisioningRecord&
        candidate,
    bool* renamed,
    std::string* error
) {
    if (renamed) {
        *renamed = false;
    }

    std::string contents;

    try {
        contents =
            record_json(candidate).dump();

        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to serialize provisioning state"
        );

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    const std::string temporary =
        temporary_filename(
            state_filename
        );

    const int raw_descriptor =
        ::openat(
            directory_descriptor,
            temporary.c_str(),
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
            "failed to reserve provisioning state temp file"
        );

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    const auto remove_temporary =
        [&]() {
            (void)::unlinkat(
                directory_descriptor,
                temporary.c_str(),
                0
            );
        };

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
                "failed to sync provisioning state temp file";
        }

        remove_temporary();

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    if (::close(
            descriptor.release()) != 0) {
        set_error(
            error,
            "failed to close provisioning state temp file"
        );

        remove_temporary();

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    const FileInspectResult
        target_result =
            inspect_file(
                directory_descriptor,
                state_filename,
                kMaximumStateFileBytes,
                nullptr,
                error
            );

    if (target_result !=
            FileInspectResult::Ok &&
        target_result !=
            FileInspectResult::Missing) {
        remove_temporary();

        return
            target_result ==
                FileInspectResult::Unsafe
            ? StorageNodeProvisioningStoreResult::
                StorageUnsafe
            : StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    if (::renameat(
            directory_descriptor,
            temporary.c_str(),
            directory_descriptor,
            state_filename.c_str()) != 0) {
        set_error(
            error,
            "failed to commit provisioning state"
        );

        remove_temporary();

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    if (renamed) {
        *renamed = true;
    }

    // Security: state transitions are successful only after the containing
    // directory durably references the committed inode.
    if (::fsync(
            directory_descriptor) != 0) {
        set_error(
            error,
            "failed to sync provisioning state directory"
        );

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    return
        StorageNodeProvisioningStoreResult::
            Ok;
}

StorageNodeProvisioningStoreResult
read_credential(
    int directory_descriptor,
    const std::string& credential_filename,
    std::string* token,
    std::string* error
) {
    if (!token) {
        set_error(
            error,
            "missing node credential output"
        );

        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    clear_internal_auth_token_text(
        token
    );

    const FileInspectResult
        inspect_result =
            inspect_file(
                directory_descriptor,
                credential_filename,
                kMaximumCredentialFileBytes,
                nullptr,
                error
            );

    if (inspect_result !=
        FileInspectResult::Ok) {
        return map_file_result(
            inspect_result
        );
    }

    if (!read_file_bounded(
            directory_descriptor,
            credential_filename,
            kMaximumCredentialFileBytes,
            token,
            true,
            error)) {
        return
            StorageNodeProvisioningStoreResult::
                IoFailed;
    }

    if (!token->empty() &&
        token->back() == '\n') {
        token->pop_back();
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
            "invalid persisted storage node credential"
        );

        return
            StorageNodeProvisioningStoreResult::
                CredentialInvalid;
    }

    return
        StorageNodeProvisioningStoreResult::
            Ok;
}

StorageNodeProvisioningStoreResult
create_credential_if_absent(
    int directory_descriptor,
    const std::string& credential_filename,
    std::string* token,
    std::string* error
) {
    if (!token) {
        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    clear_internal_auth_token_text(
        token
    );

    std::string generated;
    SensitiveStringWiper generated_wiper(
        generated
    );

    if (!generate_internal_auth_credential_token(
            &generated,
            error)) {
        return
            StorageNodeProvisioningStoreResult::
                CredentialInvalid;
    }

    std::string contents =
        generated;

    SensitiveStringWiper contents_wiper(
        contents
    );

    try {
        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to allocate node credential contents"
        );

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    const int raw_descriptor =
        ::openat(
            directory_descriptor,
            credential_filename.c_str(),
            O_WRONLY |
            O_CREAT |
            O_EXCL |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

    if (raw_descriptor < 0) {
        if (errno == EEXIST) {
            return read_credential(
                directory_descriptor,
                credential_filename,
                token,
                error
            );
        }

        set_error(
            error,
            "failed to create storage node credential"
        );

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    UniqueFd descriptor(
        raw_descriptor
    );

    const auto rollback =
        [&]() {
            (void)::unlinkat(
                directory_descriptor,
                credential_filename.c_str(),
                0
            );
        };

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
                "failed to sync storage node credential";
        }

        rollback();

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    if (::close(
            descriptor.release()) != 0) {
        set_error(
            error,
            "failed to close storage node credential"
        );

        rollback();

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    // Security: the credential is usable only after the new directory entry is
    // durable. A sync failure leaves recovery to inspect the visible file.
    if (::fsync(
            directory_descriptor) != 0) {
        set_error(
            error,
            "failed to sync storage node credential directory"
        );

        return
            StorageNodeProvisioningStoreResult::
                PersistFailed;
    }

    try {
        *token =
            generated;
    } catch (...) {
        clear_internal_auth_token_text(
            token
        );

        set_error(
            error,
            "failed to return storage node credential"
        );

        return
            StorageNodeProvisioningStoreResult::
                CredentialInvalid;
    }

    return
        StorageNodeProvisioningStoreResult::
            Ok;
}

} // namespace

const char* storage_node_provisioning_stage_name(
    StorageNodeProvisioningStage stage
) noexcept {
    return stage_text(stage);
}

const char* storage_node_provisioning_store_result_code(
    StorageNodeProvisioningStoreResult result
) noexcept {
    switch (result) {
        case StorageNodeProvisioningStoreResult::
                Ok:
            return
                "storage_node_provisioning_ok";

        case StorageNodeProvisioningStoreResult::
                InvalidInput:
            return
                "storage_node_provisioning_invalid_input";

        case StorageNodeProvisioningStoreResult::
                NotLoaded:
            return
                "storage_node_provisioning_not_loaded";

        case StorageNodeProvisioningStoreResult::
                StateMismatch:
            return
                "storage_node_provisioning_state_mismatch";

        case StorageNodeProvisioningStoreResult::
                AlreadyEnrolled:
            return
                "storage_node_already_enrolled";

        case StorageNodeProvisioningStoreResult::
                CredentialMissing:
            return
                "storage_node_credential_missing";

        case StorageNodeProvisioningStoreResult::
                CredentialInvalid:
            return
                "storage_node_credential_invalid";

        case StorageNodeProvisioningStoreResult::
                StorageUnsafe:
            return
                "storage_node_provisioning_storage_unsafe";

        case StorageNodeProvisioningStoreResult::
                IoFailed:
            return
                "storage_node_provisioning_io_failed";

        case StorageNodeProvisioningStoreResult::
                PersistFailed:
            return
                "storage_node_provisioning_persist_failed";
    }

    return
        "storage_node_provisioning_unknown";
}

bool StorageNodeProvisioningStore::load(
    const std::string& state_file,
    const std::string& credential_file,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::filesystem::path directory;
    std::string state_filename;
    std::string credential_filename;

    if (!normalize_store_paths(
            state_file,
            credential_file,
            &directory,
            &state_filename,
            &credential_filename,
            error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    UniqueFd descriptor;

    if (!ensure_private_directory(
            directory,
            &descriptor,
            error)) {
        return false;
    }

    bool state_exists = false;
    bool credential_exists = false;

    const FileInspectResult
        state_result =
            inspect_file(
                descriptor.get(),
                state_filename,
                kMaximumStateFileBytes,
                &state_exists,
                error
            );

    if (state_result !=
            FileInspectResult::Ok &&
        state_result !=
            FileInspectResult::Missing) {
        return false;
    }

    const FileInspectResult
        credential_result =
            inspect_file(
                descriptor.get(),
                credential_filename,
                kMaximumCredentialFileBytes,
                &credential_exists,
                error
            );

    if (credential_result !=
            FileInspectResult::Ok &&
        credential_result !=
            FileInspectResult::Missing) {
        return false;
    }

    std::optional<
        StorageNodeProvisioningRecord
    > loaded_record;

    if (!state_exists) {
        if (credential_exists) {
            set_error(
                error,
                "orphan storage node credential without provisioning state"
            );

            return false;
        }
    } else {
        std::string text;

        if (!read_file_bounded(
                descriptor.get(),
                state_filename,
                kMaximumStateFileBytes,
                &text,
                false,
                error)) {
            return false;
        }

        StorageNodeProvisioningRecord record;

        if (!parse_record(
                text,
                &record,
                error)) {
            return false;
        }

        if (record.stage ==
                StorageNodeProvisioningStage::
                    Prepared) {
            if (credential_exists) {
                std::string token;
                SensitiveStringWiper token_wiper(
                    token
                );

                if (read_credential(
                        descriptor.get(),
                        credential_filename,
                        &token,
                        error) !=
                    StorageNodeProvisioningStoreResult::
                        Ok) {
                    return false;
                }
            }
        } else {
            if (!credential_exists) {
                set_error(
                    error,
                    "provisioning state requires a persisted credential"
                );

                return false;
            }

            std::string token;
            SensitiveStringWiper token_wiper(
                token
            );

            if (read_credential(
                    descriptor.get(),
                    credential_filename,
                    &token,
                    error) !=
                StorageNodeProvisioningStoreResult::
                    Ok) {
                return false;
            }
        }

        loaded_record =
            std::move(record);
    }

    directory_ =
        std::move(directory);

    state_filename_ =
        std::move(state_filename);

    credential_filename_ =
        std::move(credential_filename);

    record_ =
        std::move(loaded_record);

    return true;
}

StorageNodeProvisioningStoreResult
StorageNodeProvisioningStore::
prepare_for_bootstrap(
    const StorageNodeProvisioningIntent& intent,
    const std::string& now_iso,
    std::string* node_credential,
    StorageNodeProvisioningRecord* prepared,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (node_credential) {
        clear_internal_auth_token_text(
            node_credential
        );
    }

    if (!node_credential ||
        !prepared ||
        !valid_iso_utc_seconds_text(
            now_iso)) {
        set_error(
            error,
            "invalid storage node provisioning preparation"
        );

        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    StorageNodeProvisioningIntent
        normalized_intent;

    if (!normalize_intent(
            intent,
            &normalized_intent,
            error)) {
        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if (directory_.empty() ||
        state_filename_.empty() ||
        credential_filename_.empty()) {
        set_error(
            error,
            "storage node provisioning store is not loaded"
        );

        return
            StorageNodeProvisioningStoreResult::
                NotLoaded;
    }

    UniqueFd descriptor;

    if (!ensure_private_directory(
            directory_,
            &descriptor,
            error)) {
        return
            StorageNodeProvisioningStoreResult::
                StorageUnsafe;
    }

    if (!record_.has_value()) {
        StorageNodeProvisioningRecord
            initial;

        initial.version =
            kStorageNodeProvisioningStateVersionCurrent;

        initial.stage =
            StorageNodeProvisioningStage::
                Prepared;

        initial.enrollment_id =
            normalized_intent.enrollment_id;

        initial.expected_generation =
            normalized_intent.expected_generation;

        initial.node_id =
            normalized_intent.node_id;

        initial.director_url =
            normalized_intent.director_url;

        initial.director_tls_spki_sha256_pin =
            normalized_intent
                .director_tls_spki_sha256_pin;

        initial.api_base_url =
            normalized_intent.api_base_url;

        initial.protocol_version =
            normalized_intent.protocol_version;

        initial.prepared_at =
            now_iso;

        initial.updated_at =
            now_iso;

        bool renamed = false;

        const auto persist_result =
            commit_record(
                descriptor.get(),
                state_filename_,
                initial,
                &renamed,
                error
            );

        if (renamed) {
            record_ =
                initial;
        }

        if (persist_result !=
            StorageNodeProvisioningStoreResult::
                Ok) {
            return persist_result;
        }
    }

    if (!same_intent(
            *record_,
            normalized_intent)) {
        set_error(
            error,
            "storage node provisioning intent does not match persisted state"
        );

        return
            StorageNodeProvisioningStoreResult::
                StateMismatch;
    }

    if (now_iso <
        record_->prepared_at) {
        set_error(
            error,
            "storage node provisioning timestamp precedes prepared state"
        );

        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    if (record_->stage ==
        StorageNodeProvisioningStage::
            Enrolled) {
        set_error(
            error,
            "storage node is already enrolled"
        );

        return
            StorageNodeProvisioningStoreResult::
                AlreadyEnrolled;
    }

    std::string token;
    SensitiveStringWiper token_wiper(
        token
    );

    StorageNodeProvisioningStoreResult
        credential_result;

    if (record_->stage ==
        StorageNodeProvisioningStage::
            Prepared) {
        bool exists = false;

        const FileInspectResult
            inspect_result =
                inspect_file(
                    descriptor.get(),
                    credential_filename_,
                    kMaximumCredentialFileBytes,
                    &exists,
                    error
                );

        if (inspect_result !=
                FileInspectResult::Ok &&
            inspect_result !=
                FileInspectResult::Missing) {
            return map_file_result(
                inspect_result
            );
        }

        credential_result =
            exists
            ? read_credential(
                  descriptor.get(),
                  credential_filename_,
                  &token,
                  error
              )
            : create_credential_if_absent(
                  descriptor.get(),
                  credential_filename_,
                  &token,
                  error
              );

        if (credential_result !=
            StorageNodeProvisioningStoreResult::
                Ok) {
            return credential_result;
        }

        StorageNodeProvisioningRecord
            advanced =
                *record_;

        advanced.stage =
            StorageNodeProvisioningStage::
                CredentialPersisted;

        advanced.updated_at =
            now_iso;

        bool renamed = false;

        const auto persist_result =
            commit_record(
                descriptor.get(),
                state_filename_,
                advanced,
                &renamed,
                error
            );

        if (renamed) {
            record_ =
                advanced;
        }

        if (persist_result !=
            StorageNodeProvisioningStoreResult::
                Ok) {
            return persist_result;
        }
    } else {
        credential_result =
            read_credential(
                descriptor.get(),
                credential_filename_,
                &token,
                error
            );

        if (credential_result !=
            StorageNodeProvisioningStoreResult::
                Ok) {
            return credential_result;
        }
    }

    try {
        *node_credential =
            token;
    } catch (...) {
        clear_internal_auth_token_text(
            node_credential
        );

        set_error(
            error,
            "failed to return persisted storage node credential"
        );

        return
            StorageNodeProvisioningStoreResult::
                CredentialInvalid;
    }

    *prepared =
        *record_;

    return
        StorageNodeProvisioningStoreResult::
            Ok;
}

StorageNodeProvisioningStoreResult
StorageNodeProvisioningStore::mark_enrolled(
    const StorageNodeProvisioningIntent& intent,
    const std::string& now_iso,
    StorageNodeProvisioningRecord* enrolled,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!enrolled ||
        !valid_iso_utc_seconds_text(
            now_iso)) {
        set_error(
            error,
            "invalid storage node enrolled transition"
        );

        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    StorageNodeProvisioningIntent
        normalized_intent;

    if (!normalize_intent(
            intent,
            &normalized_intent,
            error)) {
        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if (directory_.empty() ||
        !record_.has_value()) {
        set_error(
            error,
            "storage node provisioning state is unavailable"
        );

        return
            StorageNodeProvisioningStoreResult::
                NotLoaded;
    }

    if (!same_intent(
            *record_,
            normalized_intent)) {
        set_error(
            error,
            "storage node enrollment completion does not match persisted state"
        );

        return
            StorageNodeProvisioningStoreResult::
                StateMismatch;
    }

    if (now_iso <
        record_->prepared_at) {
        set_error(
            error,
            "storage node enrolled timestamp precedes prepared state"
        );

        return
            StorageNodeProvisioningStoreResult::
                InvalidInput;
    }

    if (record_->stage ==
        StorageNodeProvisioningStage::
            Enrolled) {
        *enrolled =
            *record_;

        return
            StorageNodeProvisioningStoreResult::
                Ok;
    }

    if (record_->stage !=
        StorageNodeProvisioningStage::
            CredentialPersisted) {
        set_error(
            error,
            "storage node credential is not durably prepared"
        );

        return
            StorageNodeProvisioningStoreResult::
                StateMismatch;
    }

    UniqueFd descriptor;

    if (!ensure_private_directory(
            directory_,
            &descriptor,
            error)) {
        return
            StorageNodeProvisioningStoreResult::
                StorageUnsafe;
    }

    std::string token;
    SensitiveStringWiper token_wiper(
        token
    );

    const auto credential_result =
        read_credential(
            descriptor.get(),
            credential_filename_,
            &token,
            error
        );

    if (credential_result !=
        StorageNodeProvisioningStoreResult::
            Ok) {
        return credential_result;
    }

    StorageNodeProvisioningRecord
        completed =
            *record_;

    completed.stage =
        StorageNodeProvisioningStage::
            Enrolled;

    completed.updated_at =
        now_iso;

    completed.enrolled_at =
        now_iso;

    bool renamed = false;

    const auto persist_result =
        commit_record(
            descriptor.get(),
            state_filename_,
            completed,
            &renamed,
            error
        );

    if (renamed) {
        record_ =
            completed;
    }

    if (persist_result !=
        StorageNodeProvisioningStoreResult::
            Ok) {
        return persist_result;
    }

    *enrolled =
        *record_;

    return
        StorageNodeProvisioningStoreResult::
            Ok;
}

std::optional<
    StorageNodeProvisioningRecord
> StorageNodeProvisioningStore::snapshot() const {
    std::lock_guard<std::mutex> lock(
        mutex_
    );

    return record_;
}

} // namespace pqnas::cluster
