#include "cluster/node_enrollment_registry.h"

#include "cluster/cluster_identity_validation.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <initializer_list>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pqnas::cluster {
namespace {

using json = nlohmann::json;

constexpr std::size_t
    kMaxEnrollmentRegistryBytes =
        4 * 1024 * 1024;

constexpr std::size_t
    kMaxEnrollmentRecordCount = 10000;

constexpr std::size_t
    kEnrollmentIdMinChars = 16;

constexpr std::size_t
    kEnrollmentIdMaxChars = 128;

std::atomic<std::uint64_t> g_temp_counter{0};

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

    UniqueFd(
        UniqueFd&& other
    ) noexcept
        : descriptor_(other.release()) {}

    UniqueFd& operator=(
        UniqueFd&& other
    ) noexcept {
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

void set_error(
    std::string* error,
    const std::string& message
) {
    if (error) {
        *error = message;
    }
}

bool base64url_text(
    const std::string& value,
    std::size_t minimum,
    std::size_t maximum
) {
    if (value.size() < minimum ||
        value.size() > maximum) {
        return false;
    }

    for (unsigned char ch : value) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_') {
            return false;
        }
    }

    return true;
}

bool valid_enrollment_id(
    const std::string& value
) {
    return base64url_text(
        value,
        kEnrollmentIdMinChars,
        kEnrollmentIdMaxChars
    );
}

bool valid_token_hash(
    const std::string& value
) {
    if (!base64url_text(
            value,
            kNodeEnrollmentTokenHashChars,
            kNodeEnrollmentTokenHashChars)) {
        return false;
    }

    std::array<
        unsigned char,
        kNodeEnrollmentTokenHashBytes
    > decoded{};

    std::size_t decoded_size = 0;
    const char* decode_end = nullptr;

    const int decode_result =
        sodium_base642bin(
            decoded.data(),
            decoded.size(),
            value.data(),
            value.size(),
            nullptr,
            &decoded_size,
            &decode_end,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        );

    std::array<
        char,
        sodium_base64_ENCODED_LEN(
            kNodeEnrollmentTokenHashBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        )
    > canonical{};

    const char* encode_result = nullptr;

    if (decode_result == 0 &&
        decoded_size == decoded.size() &&
        decode_end ==
            value.data() + value.size()) {
        encode_result =
            sodium_bin2base64(
                canonical.data(),
                canonical.size(),
                decoded.data(),
                decoded.size(),
                sodium_base64_VARIANT_URLSAFE_NO_PADDING
            );
    }

    // Security: decode and re-encode before accepting the verifier. This
    // rejects non-canonical encodings whose unused trailing base64 bits could
    // otherwise give one 32-byte hash multiple textual representations.
    const bool valid =
        encode_result != nullptr &&
        canonical[
            kNodeEnrollmentTokenHashChars
        ] == '\0' &&
        sodium_memcmp(
            canonical.data(),
            value.data(),
            kNodeEnrollmentTokenHashChars
        ) == 0;

    sodium_memzero(
        decoded.data(),
        decoded.size()
    );

    sodium_memzero(
        canonical.data(),
        canonical.size()
    );

    return valid;
}

bool valid_iso_utc_seconds(
    const std::string& value
) {
    if (value.size() != 20 ||
        value[4] != '-' ||
        value[7] != '-' ||
        value[10] != 'T' ||
        value[13] != ':' ||
        value[16] != ':' ||
        value[19] != 'Z') {
        return false;
    }

    constexpr std::array<std::size_t, 14>
        digit_positions{
            0, 1, 2, 3,
            5, 6,
            8, 9,
            11, 12,
            14, 15,
            17, 18
        };

    for (std::size_t position : digit_positions) {
        const unsigned char ch =
            static_cast<unsigned char>(
                value[position]
            );

        if (ch < '0' || ch > '9') {
            return false;
        }
    }

    const auto decimal =
        [&value](
            std::size_t offset,
            std::size_t count
        ) {
            int result = 0;

            for (std::size_t index = 0;
                 index < count;
                 ++index) {
                result =
                    result * 10 +
                    static_cast<int>(
                        value[offset + index] - '0'
                    );
            }

            return result;
        };

    const int year = decimal(0, 4);
    const int month = decimal(5, 2);
    const int day = decimal(8, 2);
    const int hour = decimal(11, 2);
    const int minute = decimal(14, 2);
    const int second = decimal(17, 2);

    return
        year >= 2000 &&
        year <= 9999 &&
        month >= 1 &&
        month <= 12 &&
        day >= 1 &&
        day <= 31 &&
        hour >= 0 &&
        hour <= 23 &&
        minute >= 0 &&
        minute <= 59 &&
        second >= 0 &&
        second <= 59;
}

bool has_only_keys(
    const json& object,
    std::initializer_list<const char*> allowed
) {
    if (!object.is_object()) {
        return false;
    }

    for (auto iterator = object.begin();
         iterator != object.end();
         ++iterator) {
        bool found = false;

        for (const char* key : allowed) {
            if (iterator.key() == key) {
                found = true;
                break;
            }
        }

        if (!found) {
            return false;
        }
    }

    return true;
}

bool json_string(
    const json& object,
    const char* key,
    std::string* output
) {
    if (!output) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end() ||
        !iterator->is_string()) {
        return false;
    }

    *output = iterator->get<std::string>();
    return true;
}

bool json_int(
    const json& object,
    const char* key,
    int minimum,
    int maximum,
    int* output
) {
    if (!output) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end() ||
        !iterator->is_number_integer()) {
        return false;
    }

    const std::int64_t value =
        iterator->get<std::int64_t>();

    if (value < minimum ||
        value > maximum) {
        return false;
    }

    *output = static_cast<int>(value);
    return true;
}

bool json_u64(
    const json& object,
    const char* key,
    std::uint64_t* output
) {
    if (!output) {
        return false;
    }

    const auto iterator = object.find(key);

    if (iterator == object.end()) {
        return false;
    }

    try {
        if (iterator->is_number_unsigned()) {
            *output =
                iterator->get<std::uint64_t>();
            return true;
        }

        if (iterator->is_number_integer()) {
            const std::int64_t value =
                iterator->get<std::int64_t>();

            if (value < 0) {
                return false;
            }

            *output =
                static_cast<std::uint64_t>(
                    value
                );

            return true;
        }
    } catch (...) {
        return false;
    }

    return false;
}

bool parse_state(
    const std::string& value,
    NodeEnrollmentState* output
) {
    if (!output) {
        return false;
    }

    if (value == "pending") {
        *output = NodeEnrollmentState::Pending;
        return true;
    }

    if (value == "revoked") {
        *output = NodeEnrollmentState::Revoked;
        return true;
    }

    if (value == "consumed") {
        *output = NodeEnrollmentState::Consumed;
        return true;
    }

    return false;
}

bool normalize_registry_path(
    const std::string& raw,
    std::filesystem::path* normalized,
    std::string* error
) {
    if (!normalized) {
        set_error(
            error,
            "missing enrollment registry path output"
        );
        return false;
    }

    if (raw.empty() || raw.size() > 4096) {
        set_error(
            error,
            "invalid enrollment registry path"
        );
        return false;
    }

    const std::filesystem::path path(raw);

    if (!path.is_absolute() ||
        !path.has_root_path()) {
        set_error(
            error,
            "enrollment registry path must be absolute"
        );
        return false;
    }

    for (const auto& component : path) {
        const std::string part =
            component.string();

        if (part == "." || part == "..") {
            set_error(
                error,
                "enrollment registry path contains traversal"
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
            "enrollment registry path must name a file"
        );
        return false;
    }

    *normalized = clean;
    return true;
}

bool validate_record(
    const NodeEnrollmentRecord& record,
    std::string* error
) {
    if (!valid_enrollment_id(
            record.enrollment_id) ||
        !valid_cluster_node_id(
            record.node_id) ||
        record.required_protocol_version < 1 ||
        record.required_protocol_version > 1000 ||
        record.generation == 0 ||
        !valid_iso_utc_seconds(
            record.created_at) ||
        !valid_iso_utc_seconds(
            record.expires_at) ||
        record.created_at >= record.expires_at) {
        set_error(
            error,
            "invalid enrollment record metadata"
        );
        return false;
    }

    std::string normalized_url;

    if (!normalize_cluster_https_base_url(
            record.api_base_url,
            &normalized_url,
            error) ||
        normalized_url != record.api_base_url) {
        if (error && error->empty()) {
            *error =
                "non-canonical enrollment API URL";
        }

        return false;
    }

    switch (record.state) {
        case NodeEnrollmentState::Pending:
            if (!valid_token_hash(
                    record.token_hash) ||
                !record.revoked_at.empty() ||
                !record.consumed_at.empty()) {
                set_error(
                    error,
                    "invalid pending enrollment state"
                );
                return false;
            }

            break;

        case NodeEnrollmentState::Revoked:
            if (!record.token_hash.empty() ||
                !valid_iso_utc_seconds(
                    record.revoked_at) ||
                record.revoked_at <
                    record.created_at ||
                !record.consumed_at.empty()) {
                set_error(
                    error,
                    "invalid revoked enrollment state"
                );
                return false;
            }

            break;

        case NodeEnrollmentState::Consumed:
            if (!record.token_hash.empty() ||
                !record.revoked_at.empty() ||
                !valid_iso_utc_seconds(
                    record.consumed_at) ||
                record.consumed_at <
                    record.created_at ||
                record.consumed_at >=
                    record.expires_at) {
                set_error(
                    error,
                    "invalid consumed enrollment state"
                );
                return false;
            }

            break;
    }

    return true;
}

bool validate_record_set(
    const std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    >& records,
    std::string* error
) {
    if (records.size() >
        kMaxEnrollmentRecordCount) {
        set_error(
            error,
            "too many enrollment records"
        );
        return false;
    }

    std::unordered_map<
        std::string,
        std::unordered_set<std::uint64_t>
    > generations;

    std::unordered_map<
        std::string,
        std::uint64_t
    > maximum_generation;

    std::unordered_set<std::string>
        pending_nodes;

    std::unordered_set<std::string>
        pending_urls;

    for (const auto& entry : records) {
        const NodeEnrollmentRecord& record =
            entry.second;

        if (entry.first != record.enrollment_id ||
            !validate_record(record, error)) {
            if (error && error->empty()) {
                *error =
                    "invalid enrollment registry entry";
            }

            return false;
        }

        if (!generations[record.node_id]
                .insert(record.generation)
                .second) {
            set_error(
                error,
                "duplicate node enrollment generation"
            );
            return false;
        }

        auto maximum_iterator =
            maximum_generation.find(
                record.node_id
            );

        if (maximum_iterator ==
                maximum_generation.end() ||
            record.generation >
                maximum_iterator->second) {
            maximum_generation[record.node_id] =
                record.generation;
        }

        if (record.state ==
            NodeEnrollmentState::Pending) {
            if (!pending_nodes
                    .insert(record.node_id)
                    .second) {
                set_error(
                    error,
                    "multiple pending enrollments for node"
                );
                return false;
            }

            if (!pending_urls
                    .insert(record.api_base_url)
                    .second) {
                set_error(
                    error,
                    "multiple pending enrollments for URL"
                );
                return false;
            }
        }
    }

    for (const auto& entry : records) {
        const NodeEnrollmentRecord& record =
            entry.second;

        if (record.state ==
                NodeEnrollmentState::Pending &&
            maximum_generation.at(
                record.node_id) !=
                record.generation) {
            set_error(
                error,
                "pending enrollment is not latest generation"
            );
            return false;
        }
    }

    return true;
}

json record_to_json(
    const NodeEnrollmentRecord& record
) {
    return json{
        {
            "enrollment_id",
            record.enrollment_id
        },
        {
            "node_id",
            record.node_id
        },
        {
            "api_base_url",
            record.api_base_url
        },
        {
            "required_protocol_version",
            record.required_protocol_version
        },
        {
            "token_hash",
            record.token_hash
        },
        {
            "created_at",
            record.created_at
        },
        {
            "expires_at",
            record.expires_at
        },
        {
            "state",
            node_enrollment_state_name(
                record.state
            )
        },
        {
            "revoked_at",
            record.revoked_at
        },
        {
            "consumed_at",
            record.consumed_at
        },
        {
            "generation",
            record.generation
        }
    };
}

bool parse_record(
    const json& value,
    NodeEnrollmentRecord* output,
    std::string* error
) {
    if (!output ||
        !has_only_keys(
            value,
            {
                "enrollment_id",
                "node_id",
                "api_base_url",
                "required_protocol_version",
                "token_hash",
                "created_at",
                "expires_at",
                "state",
                "revoked_at",
                "consumed_at",
                "generation"
            })) {
        set_error(
            error,
            "invalid enrollment registry entry"
        );
        return false;
    }

    NodeEnrollmentRecord record;
    std::string state_name;

    if (!json_string(
            value,
            "enrollment_id",
            &record.enrollment_id) ||
        !json_string(
            value,
            "node_id",
            &record.node_id) ||
        !json_string(
            value,
            "api_base_url",
            &record.api_base_url) ||
        !json_int(
            value,
            "required_protocol_version",
            1,
            1000,
            &record.required_protocol_version) ||
        !json_string(
            value,
            "token_hash",
            &record.token_hash) ||
        !json_string(
            value,
            "created_at",
            &record.created_at) ||
        !json_string(
            value,
            "expires_at",
            &record.expires_at) ||
        !json_string(
            value,
            "state",
            &state_name) ||
        !parse_state(
            state_name,
            &record.state) ||
        !json_string(
            value,
            "revoked_at",
            &record.revoked_at) ||
        !json_string(
            value,
            "consumed_at",
            &record.consumed_at) ||
        !json_u64(
            value,
            "generation",
            &record.generation) ||
        !validate_record(
            record,
            error)) {
        if (error && error->empty()) {
            *error =
                "invalid enrollment registry entry";
        }

        return false;
    }

    *output = std::move(record);
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
            "failed to inspect enrollment registry directory"
        );
        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    // Security: trusted enrollment state must not live in an attacker-writable
    // directory where the target or temporary file can be swapped.
    if (!S_ISDIR(stats.st_mode) ||
        (stats.st_mode & 0022) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe enrollment registry directory"
        );
        return false;
    }

    return true;
}

bool validate_existing_target(
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
            return true;
        }

        set_error(
            error,
            "failed to inspect enrollment registry target"
        );
        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    // Security: reject symlinks, hardlinks, devices and files visible to
    // group/other users before atomically replacing trusted verifier state.
    if (!S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        (stats.st_mode & 0077) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe enrollment registry target"
        );
        return false;
    }

    return true;
}

bool read_bounded_file(
    int descriptor,
    std::string* output,
    std::string* error
) {
    if (!output) {
        set_error(
            error,
            "missing enrollment registry buffer"
        );
        return false;
    }

    output->clear();

    std::array<char, 8192> buffer{};

    while (true) {
        const ssize_t count =
            ::read(
                descriptor,
                buffer.data(),
                buffer.size()
            );

        if (count > 0) {
            const std::size_t bytes =
                static_cast<std::size_t>(
                    count
                );

            if (output->size() >
                kMaxEnrollmentRegistryBytes -
                    std::min(
                        bytes,
                        kMaxEnrollmentRegistryBytes)) {
                set_error(
                    error,
                    "enrollment registry is too large"
                );
                return false;
            }

            output->append(
                buffer.data(),
                bytes
            );

            if (output->size() >
                kMaxEnrollmentRegistryBytes) {
                set_error(
                    error,
                    "enrollment registry is too large"
                );
                return false;
            }

            continue;
        }

        if (count == 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to read enrollment registry"
        );
        return false;
    }
}

bool read_registry_file(
    const std::filesystem::path& path,
    std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    >* output,
    bool* missing,
    std::string* error
) {
    if (!output || !missing) {
        set_error(
            error,
            "missing enrollment registry load output"
        );
        return false;
    }

    *missing = false;

    const int raw_directory_descriptor =
        ::open(
            path.parent_path().c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_directory_descriptor < 0) {
        if (errno == ENOENT) {
            output->clear();
            *missing = true;
            return true;
        }

        set_error(
            error,
            "failed to open enrollment registry directory"
        );
        return false;
    }

    UniqueFd directory_descriptor(
        raw_directory_descriptor
    );

    if (!validate_directory_descriptor(
            directory_descriptor.get(),
            error)) {
        return false;
    }

    const std::string filename =
        path.filename().string();

    const int raw_file_descriptor =
        ::openat(
            directory_descriptor.get(),
            filename.c_str(),
            O_RDONLY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_file_descriptor < 0) {
        if (errno == ENOENT) {
            output->clear();
            *missing = true;
            return true;
        }

        set_error(
            error,
            "failed to open enrollment registry"
        );
        return false;
    }

    UniqueFd file_descriptor(
        raw_file_descriptor
    );

    struct stat stats {};

    if (::fstat(
            file_descriptor.get(),
            &stats) != 0) {
        set_error(
            error,
            "failed to inspect enrollment registry"
        );
        return false;
    }

    const uid_t effective_uid =
        ::geteuid();

    if (!S_ISREG(stats.st_mode) ||
        stats.st_nlink != 1 ||
        (stats.st_mode & 0077) != 0 ||
        (stats.st_uid != effective_uid &&
         stats.st_uid != 0)) {
        set_error(
            error,
            "unsafe enrollment registry file"
        );
        return false;
    }

    if (stats.st_size < 0 ||
        static_cast<unsigned long long>(
            stats.st_size) >
        static_cast<unsigned long long>(
            kMaxEnrollmentRegistryBytes)) {
        set_error(
            error,
            "enrollment registry has invalid size"
        );
        return false;
    }

    std::string contents;

    if (!read_bounded_file(
            file_descriptor.get(),
            &contents,
            error)) {
        return false;
    }

    json root;

    try {
        root = json::parse(contents);
    } catch (...) {
        set_error(
            error,
            "invalid enrollment registry JSON"
        );
        return false;
    }

    if (!has_only_keys(
            root,
            {"version", "enrollments"})) {
        set_error(
            error,
            "invalid enrollment registry root"
        );
        return false;
    }

    int version = 0;

    if (!json_int(
            root,
            "version",
            kNodeEnrollmentRegistrySchemaVersion,
            kNodeEnrollmentRegistrySchemaVersion,
            &version)) {
        set_error(
            error,
            "unsupported enrollment registry version"
        );
        return false;
    }

    (void)version;

    const auto records_iterator =
        root.find("enrollments");

    if (records_iterator == root.end() ||
        !records_iterator->is_array() ||
        records_iterator->size() >
            kMaxEnrollmentRecordCount) {
        set_error(
            error,
            "invalid enrollment registry array"
        );
        return false;
    }

    std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    > parsed;

    parsed.reserve(
        records_iterator->size()
    );

    for (const json& value :
         *records_iterator) {
        NodeEnrollmentRecord record;

        if (!parse_record(
                value,
                &record,
                error)) {
            return false;
        }

        const std::string enrollment_id =
            record.enrollment_id;

        if (!parsed.emplace(
                enrollment_id,
                std::move(record))
                .second) {
            set_error(
                error,
                "duplicate enrollment ID"
            );
            return false;
        }
    }

    if (!validate_record_set(
            parsed,
            error)) {
        return false;
    }

    *output = std::move(parsed);
    return true;
}

bool write_all(
    int descriptor,
    const std::string& contents,
    std::string* error
) {
    std::size_t offset = 0;

    while (offset < contents.size()) {
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

        if (count < 0 && errno == EINTR) {
            continue;
        }

        set_error(
            error,
            "failed to write enrollment registry"
        );
        return false;
    }

    return true;
}

bool persist_registry_file(
    const std::filesystem::path& path,
    const std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    >& records,
    std::string* error
) {
    if (!validate_record_set(
            records,
            error)) {
        return false;
    }

    std::vector<std::string>
        enrollment_ids;

    enrollment_ids.reserve(
        records.size()
    );

    for (const auto& entry : records) {
        enrollment_ids.push_back(
            entry.first
        );
    }

    std::sort(
        enrollment_ids.begin(),
        enrollment_ids.end()
    );

    json root{
        {
            "version",
            kNodeEnrollmentRegistrySchemaVersion
        },
        {
            "enrollments",
            json::array()
        }
    };

    for (const std::string& enrollment_id :
         enrollment_ids) {
        root["enrollments"].push_back(
            record_to_json(
                records.at(enrollment_id)
            )
        );
    }

    std::string contents;

    try {
        contents = root.dump(2);
        contents.push_back('\n');
    } catch (...) {
        set_error(
            error,
            "failed to serialize enrollment registry"
        );
        return false;
    }

    if (contents.size() >
        kMaxEnrollmentRegistryBytes) {
        set_error(
            error,
            "serialized enrollment registry is too large"
        );
        return false;
    }

    std::error_code filesystem_error;

    const bool created_registry_directory =
        std::filesystem::create_directories(
            path.parent_path(),
            filesystem_error
        );

    if (filesystem_error) {
        set_error(
            error,
            "failed to create enrollment registry directory"
        );
        return false;
    }

    const int raw_directory_descriptor =
        ::open(
            path.parent_path().c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    if (raw_directory_descriptor < 0) {
        set_error(
            error,
            "failed to open enrollment registry directory"
        );
        return false;
    }

    UniqueFd directory_descriptor(
        raw_directory_descriptor
    );

    if (created_registry_directory) {
        struct stat created_directory_stats {};

        // Security: operate only on the opened inode and confirm ownership
        // before changing permissions. Existing administrator-managed
        // directories are never modified automatically.
        if (::fstat(
                directory_descriptor.get(),
                &created_directory_stats) != 0 ||
            !S_ISDIR(
                created_directory_stats.st_mode) ||
            created_directory_stats.st_uid !=
                ::geteuid()) {
            set_error(
                error,
                "unsafe newly created enrollment registry directory"
            );
            return false;
        }

        // Security: umask may create a group-accessible directory. Enrollment
        // verifier state must live in a service-private directory.
        if (::fchmod(
                directory_descriptor.get(),
                0700) != 0) {
            set_error(
                error,
                "failed to secure enrollment registry directory"
            );
            return false;
        }
    }

    if (!validate_directory_descriptor(
            directory_descriptor.get(),
            error)) {
        return false;
    }

    const std::string target_filename =
        path.filename().string();

    if (!validate_existing_target(
            directory_descriptor.get(),
            target_filename,
            error)) {
        return false;
    }

    std::string temporary_filename;
    UniqueFd temporary_descriptor;

    for (int attempt = 0;
         attempt < 16;
         ++attempt) {
        const std::uint64_t counter =
            g_temp_counter.fetch_add(1);

        temporary_filename =
            "." +
            target_filename +
            ".tmp." +
            std::to_string(
                static_cast<long long>(
                    ::getpid()
                )
            ) +
            "." +
            std::to_string(counter);

        const int raw_temporary_descriptor =
            ::openat(
                directory_descriptor.get(),
                temporary_filename.c_str(),
                O_WRONLY |
                O_CREAT |
                O_EXCL |
                O_CLOEXEC |
                O_NOFOLLOW,
                0600
            );

        if (raw_temporary_descriptor >= 0) {
            temporary_descriptor =
                UniqueFd(
                    raw_temporary_descriptor
                );
            break;
        }

        if (errno != EEXIST) {
            set_error(
                error,
                "failed to create enrollment registry temp file"
            );
            return false;
        }
    }

    if (temporary_descriptor.get() < 0) {
        set_error(
            error,
            "failed to allocate enrollment registry temp file"
        );
        return false;
    }

    const auto remove_temporary = [&]() {
        if (!temporary_filename.empty()) {
            (void)::unlinkat(
                directory_descriptor.get(),
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
            *error =
                "failed to sync enrollment registry";
        }

        remove_temporary();
        return false;
    }

    const int raw_temporary_descriptor =
        temporary_descriptor.release();

    if (::close(
            raw_temporary_descriptor) != 0) {
        set_error(
            error,
            "failed to close enrollment registry temp file"
        );
        remove_temporary();
        return false;
    }

    // Security: re-check immediately before commit. renameat never follows the
    // target, but this also fails closed if an unsafe entry appeared meanwhile.
    if (!validate_existing_target(
            directory_descriptor.get(),
            target_filename,
            error)) {
        remove_temporary();
        return false;
    }

    if (::renameat(
            directory_descriptor.get(),
            temporary_filename.c_str(),
            directory_descriptor.get(),
            target_filename.c_str()) != 0) {
        set_error(
            error,
            "failed to commit enrollment registry"
        );
        remove_temporary();
        return false;
    }

    // The logical commit has completed. Directory fsync is best-effort
    // durability hardening and must not roll memory back after rename.
    (void)::fsync(
        directory_descriptor.get()
    );

    return true;
}

NodeEnrollmentRegistryResult semantic_failure(
    NodeEnrollmentRegistryResult result,
    std::string* error
) {
    set_error(
        error,
        node_enrollment_registry_result_code(
            result
        )
    );

    return result;
}

} // namespace

const char* node_enrollment_state_name(
    NodeEnrollmentState state
) {
    switch (state) {
        case NodeEnrollmentState::Pending:
            return "pending";

        case NodeEnrollmentState::Revoked:
            return "revoked";

        case NodeEnrollmentState::Consumed:
            return "consumed";
    }

    return "unknown";
}

const char* node_enrollment_registry_result_code(
    NodeEnrollmentRegistryResult result
) {
    switch (result) {
        case NodeEnrollmentRegistryResult::Ok:
            return "ok";

        case NodeEnrollmentRegistryResult::InvalidInput:
            return "enrollment_invalid";

        case NodeEnrollmentRegistryResult::NotFound:
            return "enrollment_not_found";

        case NodeEnrollmentRegistryResult::
                DuplicateEnrollmentId:
            return "enrollment_id_conflict";

        case NodeEnrollmentRegistryResult::
                DuplicateActiveUrl:
            return "enrollment_url_conflict";

        case NodeEnrollmentRegistryResult::
                GenerationMismatch:
            return "enrollment_generation_mismatch";

        case NodeEnrollmentRegistryResult::Expired:
            return "enrollment_expired";

        case NodeEnrollmentRegistryResult::Revoked:
            return "enrollment_revoked";

        case NodeEnrollmentRegistryResult::
                AlreadyConsumed:
            return "enrollment_already_consumed";

        case NodeEnrollmentRegistryResult::
                NodeMismatch:
            return "enrollment_node_mismatch";

        case NodeEnrollmentRegistryResult::
                UrlMismatch:
            return "enrollment_url_mismatch";

        case NodeEnrollmentRegistryResult::
                ProtocolIncompatible:
            return "enrollment_protocol_incompatible";

        case NodeEnrollmentRegistryResult::
                TokenInvalid:
            return "enrollment_token_invalid";

        case NodeEnrollmentRegistryResult::
                PersistFailed:
            return "enrollment_persist_failed";
    }

    return "enrollment_invalid";
}

bool NodeEnrollmentRegistry::load(
    const std::string& path,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::filesystem::path normalized_path;

    if (!normalize_registry_path(
            path,
            &normalized_path,
            error)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    > parsed;

    bool missing = false;

    if (!read_registry_file(
            normalized_path,
            &parsed,
            &missing,
            error)) {
        return false;
    }

    (void)missing;

    path_ = std::move(normalized_path);
    records_ = std::move(parsed);
    return true;
}

std::optional<NodeEnrollmentRecord>
NodeEnrollmentRegistry::get(
    const std::string& enrollment_id
) const {
    std::lock_guard<std::mutex> lock(
        mutex_
    );

    const auto iterator =
        records_.find(enrollment_id);

    if (iterator == records_.end()) {
        return std::nullopt;
    }

    return iterator->second;
}

std::vector<NodeEnrollmentRecord>
NodeEnrollmentRegistry::snapshot() const {
    std::lock_guard<std::mutex> lock(
        mutex_
    );

    std::vector<NodeEnrollmentRecord>
        result;

    result.reserve(records_.size());

    for (const auto& entry : records_) {
        result.push_back(entry.second);
    }

    std::sort(
        result.begin(),
        result.end(),
        [](
            const NodeEnrollmentRecord& left,
            const NodeEnrollmentRecord& right
        ) {
            if (left.node_id != right.node_id) {
                return left.node_id <
                    right.node_id;
            }

            if (left.generation !=
                right.generation) {
                return left.generation <
                    right.generation;
            }

            return left.enrollment_id <
                right.enrollment_id;
        }
    );

    return result;
}

bool NodeEnrollmentRegistry::
persist_candidate_locked(
    const std::unordered_map<
        std::string,
        NodeEnrollmentRecord
    >& candidate,
    std::string* error
) const {
    if (path_.empty()) {
        set_error(
            error,
            "enrollment registry is not loaded"
        );
        return false;
    }

    return persist_registry_file(
        path_,
        candidate,
        error
    );
}

NodeEnrollmentRegistryResult
NodeEnrollmentRegistry::
create_or_replace_pending(
    const std::string& enrollment_id,
    const std::string& node_id,
    const std::string& api_base_url,
    int required_protocol_version,
    const std::string& token_hash,
    const std::string& created_at,
    const std::string& expires_at,
    NodeEnrollmentRecord* created,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::string normalized_url;

    if (!valid_enrollment_id(
            enrollment_id) ||
        !valid_cluster_node_id(
            node_id) ||
        required_protocol_version < 1 ||
        required_protocol_version > 1000 ||
        !valid_token_hash(
            token_hash) ||
        !valid_iso_utc_seconds(
            created_at) ||
        !valid_iso_utc_seconds(
            expires_at) ||
        created_at >= expires_at ||
        !normalize_cluster_https_base_url(
            api_base_url,
            &normalized_url,
            error)) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                InvalidInput,
            error
        );
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    if (records_.find(enrollment_id) !=
        records_.end()) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                DuplicateEnrollmentId,
            error
        );
    }

    std::uint64_t maximum_generation = 0;
    std::string latest_created_at;

    for (const auto& entry : records_) {
        const NodeEnrollmentRecord& record =
            entry.second;

        if (record.node_id != node_id) {
            continue;
        }

        maximum_generation =
            std::max(
                maximum_generation,
                record.generation
            );

        if (latest_created_at.empty() ||
            record.created_at >
                latest_created_at) {
            latest_created_at =
                record.created_at;
        }
    }

    if ((!latest_created_at.empty() &&
         created_at < latest_created_at) ||
        maximum_generation ==
            std::numeric_limits<
                std::uint64_t>::max()) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                InvalidInput,
            error
        );
    }

    auto candidate = records_;

    for (auto& entry : candidate) {
        NodeEnrollmentRecord& record =
            entry.second;

        if (record.state !=
            NodeEnrollmentState::Pending) {
            continue;
        }

        const bool same_node =
            record.node_id == node_id;

        const bool same_url =
            record.api_base_url ==
                normalized_url;

        if (!same_node && !same_url) {
            continue;
        }

        if (same_url &&
            !same_node &&
            record.expires_at >
                created_at) {
            return semantic_failure(
                NodeEnrollmentRegistryResult::
                    DuplicateActiveUrl,
                error
            );
        }

        // Security: replacement invalidates the old verifier in the same
        // persisted transaction that installs the new pending intent.
        record.state =
            NodeEnrollmentState::Revoked;

        record.revoked_at = created_at;
        record.consumed_at.clear();
        record.token_hash.clear();
    }

    NodeEnrollmentRecord record;
    record.enrollment_id = enrollment_id;
    record.node_id = node_id;
    record.api_base_url =
        std::move(normalized_url);

    record.required_protocol_version =
        required_protocol_version;

    record.token_hash = token_hash;
    record.created_at = created_at;
    record.expires_at = expires_at;

    record.state =
        NodeEnrollmentState::Pending;

    record.generation =
        maximum_generation + 1;

    candidate.emplace(
        record.enrollment_id,
        record
    );

    if (!persist_candidate_locked(
            candidate,
            error)) {
        return NodeEnrollmentRegistryResult::
            PersistFailed;
    }

    records_ = std::move(candidate);

    if (created) {
        *created = record;
    }

    return NodeEnrollmentRegistryResult::Ok;
}

NodeEnrollmentRegistryResult
NodeEnrollmentRegistry::revoke_pending(
    const std::string& enrollment_id,
    std::uint64_t expected_generation,
    const std::string& revoked_at,
    NodeEnrollmentRecord* revoked,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    if (!valid_enrollment_id(
            enrollment_id) ||
        expected_generation == 0 ||
        !valid_iso_utc_seconds(
            revoked_at)) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                InvalidInput,
            error
        );
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    const auto iterator =
        records_.find(enrollment_id);

    if (iterator == records_.end()) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                NotFound,
            error
        );
    }

    const NodeEnrollmentRecord& existing =
        iterator->second;

    if (existing.generation !=
        expected_generation) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                GenerationMismatch,
            error
        );
    }

    if (existing.state ==
        NodeEnrollmentState::Revoked) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                Revoked,
            error
        );
    }

    if (existing.state ==
        NodeEnrollmentState::Consumed) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                AlreadyConsumed,
            error
        );
    }

    if (revoked_at < existing.created_at) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                InvalidInput,
            error
        );
    }

    auto candidate = records_;
    NodeEnrollmentRecord& updated =
        candidate.at(enrollment_id);

    updated.state =
        NodeEnrollmentState::Revoked;

    updated.revoked_at = revoked_at;
    updated.consumed_at.clear();

    // Security: revocation removes the persisted verifier so the old
    // bootstrap token cannot be validated again through another code path.
    updated.token_hash.clear();

    if (!persist_candidate_locked(
            candidate,
            error)) {
        return NodeEnrollmentRegistryResult::
            PersistFailed;
    }

    records_ = std::move(candidate);

    if (revoked) {
        *revoked =
            records_.at(enrollment_id);
    }

    return NodeEnrollmentRegistryResult::Ok;
}

NodeEnrollmentRegistryResult
NodeEnrollmentRegistry::
consume_pending_if_matches(
    const std::string& enrollment_id,
    const std::string& expected_node_id,
    const std::string& expected_api_base_url,
    int presented_protocol_version,
    std::uint64_t expected_generation,
    const std::string& presented_token_hash,
    const std::string& consumed_at,
    NodeEnrollmentRecord* consumed,
    std::string* error
) {
    if (error) {
        error->clear();
    }

    std::string normalized_url;

    if (!valid_enrollment_id(
            enrollment_id) ||
        !valid_cluster_node_id(
            expected_node_id) ||
        presented_protocol_version < 1 ||
        presented_protocol_version > 1000 ||
        expected_generation == 0 ||
        !valid_token_hash(
            presented_token_hash) ||
        !valid_iso_utc_seconds(
            consumed_at) ||
        !normalize_cluster_https_base_url(
            expected_api_base_url,
            &normalized_url,
            error)) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                InvalidInput,
            error
        );
    }

    std::lock_guard<std::mutex> lock(
        mutex_
    );

    const auto iterator =
        records_.find(enrollment_id);

    if (iterator == records_.end()) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                NotFound,
            error
        );
    }

    const NodeEnrollmentRecord& existing =
        iterator->second;

    if (existing.state ==
        NodeEnrollmentState::Revoked) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                Revoked,
            error
        );
    }

    if (existing.state ==
        NodeEnrollmentState::Consumed) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                AlreadyConsumed,
            error
        );
    }

    if (consumed_at < existing.created_at) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                InvalidInput,
            error
        );
    }

    if (consumed_at >= existing.expires_at) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                Expired,
            error
        );
    }

    if (existing.generation !=
        expected_generation) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                GenerationMismatch,
            error
        );
    }

    if (existing.node_id !=
        expected_node_id) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                NodeMismatch,
            error
        );
    }

    if (existing.api_base_url !=
        normalized_url) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                UrlMismatch,
            error
        );
    }

    if (existing.required_protocol_version !=
        presented_protocol_version) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                ProtocolIncompatible,
            error
        );
    }

    // Both strings have already been validated to the same fixed size.
    // sodium_memcmp prevents an early-exit timing signal from token matching.
    if (sodium_memcmp(
            existing.token_hash.data(),
            presented_token_hash.data(),
            kNodeEnrollmentTokenHashChars) != 0) {
        return semantic_failure(
            NodeEnrollmentRegistryResult::
                TokenInvalid,
            error
        );
    }

    auto candidate = records_;
    NodeEnrollmentRecord& updated =
        candidate.at(enrollment_id);

    updated.state =
        NodeEnrollmentState::Consumed;

    updated.consumed_at = consumed_at;
    updated.revoked_at.clear();

    // Security: one successful consume deletes the verifier in the same
    // persistent transaction, making replay fail on terminal state.
    updated.token_hash.clear();

    if (!persist_candidate_locked(
            candidate,
            error)) {
        return NodeEnrollmentRegistryResult::
            PersistFailed;
    }

    records_ = std::move(candidate);

    if (consumed) {
        *consumed =
            records_.at(enrollment_id);
    }

    return NodeEnrollmentRegistryResult::Ok;
}

} // namespace pqnas::cluster
