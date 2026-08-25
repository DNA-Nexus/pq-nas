#include "cluster/storage_node_provisioning_store.h"

#include "cluster/internal_auth.h"

#include <nlohmann/json.hpp>
#include <sodium.h>

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

using pqnas::cluster::
    StorageNodeProvisioningIntent;
using pqnas::cluster::
    StorageNodeProvisioningRecord;
using pqnas::cluster::
    StorageNodeProvisioningStage;
using pqnas::cluster::
    StorageNodeProvisioningStore;
using pqnas::cluster::
    StorageNodeProvisioningStoreResult;

int failures = 0;

constexpr char kTestDirectorTlsPin[] =
    "sha256/"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

void expect_result(
    StorageNodeProvisioningStoreResult actual,
    StorageNodeProvisioningStoreResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            " actual=" +
            pqnas::cluster::
                storage_node_provisioning_store_result_code(
                    actual
                ) +
            " expected=" +
            pqnas::cluster::
                storage_node_provisioning_store_result_code(
                    expected
                )
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-storage-node-provisioning-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* created =
        ::mkdtemp(
            writable.data()
        );

    return created
        ? fs::path(created)
        : fs::path{};
}

StorageNodeProvisioningIntent
make_intent() {
    StorageNodeProvisioningIntent intent;

    intent.enrollment_id =
        "enrollment_storage_node_01";

    intent.expected_generation =
        7;

    intent.node_id =
        "storage-node-01";

    intent.director_url =
        "https://director.internal/";

    intent.director_tls_spki_sha256_pin =
        kTestDirectorTlsPin;

    intent.api_base_url =
        "https://storage-node-01.internal/";

    intent.protocol_version =
        1;

    return intent;
}

json state_json(
    const StorageNodeProvisioningIntent&
        intent,
    const std::string& stage,
    const std::string& prepared_at,
    const std::string& updated_at,
    const std::string& enrolled_at =
        ""
) {
    return json{
        {
            "version",
            pqnas::cluster::
                kStorageNodeProvisioningStateVersionCurrent
        },
        {"stage", stage},
        {
            "enrollment_id",
            intent.enrollment_id
        },
        {
            "expected_generation",
            intent.expected_generation
        },
        {"node_id", intent.node_id},
        {
            "director_url",
            "https://director.internal"
        },
        {
            "director_tls_spki_sha256_pin",
            intent.director_tls_spki_sha256_pin
        },
        {
            "api_base_url",
            "https://storage-node-01.internal"
        },
        {
            "protocol_version",
            intent.protocol_version
        },
        {
            "prepared_at",
            prepared_at
        },
        {
            "updated_at",
            updated_at
        },
        {
            "enrolled_at",
            enrolled_at
        }
    };
}

bool write_private_text(
    const fs::path& path,
    const std::string& text
) {
    const int descriptor =
        ::open(
            path.c_str(),
            O_WRONLY |
            O_CREAT |
            O_TRUNC |
            O_CLOEXEC |
            O_NOFOLLOW,
            0600
        );

    if (descriptor < 0) {
        return false;
    }

    std::size_t offset = 0;

    while (offset <
           text.size()) {
        const ssize_t count =
            ::write(
                descriptor,
                text.data() + offset,
                text.size() - offset
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

        (void)::close(descriptor);
        return false;
    }

    const bool synced =
        ::fsync(descriptor) == 0;

    const bool closed =
        ::close(descriptor) == 0;

    return synced && closed;
}

std::string read_text(
    const fs::path& path
) {
    std::ifstream input(path);

    if (!input) {
        return {};
    }

    return std::string(
        std::istreambuf_iterator<char>(
            input
        ),
        std::istreambuf_iterator<char>()
    );
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";

        return 1;
    }

    const fs::path root =
        make_test_directory();

    if (root.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    const fs::path private_directory =
        root /
        "private";

    const fs::path state_file =
        private_directory /
        "storage_node_provisioning.json";

    const fs::path credential_file =
        private_directory /
        "cluster_node.credential";

    std::string error;

    StorageNodeProvisioningStore store;

    expect(
        store.load(
            state_file.string(),
            credential_file.string(),
            &error
        ),
        "load empty provisioning store: " +
            error
    );

    expect(
        !store.snapshot().has_value(),
        "empty provisioning store snapshot"
    );

    const StorageNodeProvisioningIntent
        intent =
            make_intent();

    StorageNodeProvisioningRecord prepared;
    std::string credential;

    StorageNodeProvisioningIntent
        noncanonical_director_tls =
            intent;

    noncanonical_director_tls
        .director_tls_spki_sha256_pin =
            " " +
            intent.director_tls_spki_sha256_pin;

    expect_result(
        store.prepare_for_bootstrap(
            noncanonical_director_tls,
            "2026-07-31T03:59:59Z",
            &credential,
            &prepared,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            InvalidInput,
        "reject noncanonical Director TLS identity before credential creation"
    );

    expect(
        credential.empty() &&
        !store.snapshot().has_value(),
        "noncanonical Director TLS identity must not persist state or credential"
    );

    error.clear();

    expect_result(
        store.prepare_for_bootstrap(
            intent,
            "2026-07-31T04:00:00Z",
            &credential,
            &prepared,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            Ok,
        "prepare storage node credential"
    );

    expect(
        prepared.stage ==
            StorageNodeProvisioningStage::
                CredentialPersisted &&
        prepared.director_url ==
            "https://director.internal" &&
        prepared.api_base_url ==
            "https://storage-node-01.internal" &&
        pqnas::cluster::
            valid_internal_auth_credential_token(
                credential
            ),
        "prepared canonical state and credential"
    );

    std::string first_credential =
        credential;

    pqnas::cluster::
        clear_internal_auth_token_text(
            &credential
        );

    struct stat directory_stats {};
    struct stat state_stats {};
    struct stat credential_stats {};

    expect(
        ::stat(
            private_directory.c_str(),
            &directory_stats) == 0 &&
        S_ISDIR(directory_stats.st_mode) &&
        (directory_stats.st_mode & 0077) == 0,
        "provisioning directory is private"
    );

    expect(
        ::lstat(
            state_file.c_str(),
            &state_stats) == 0 &&
        S_ISREG(state_stats.st_mode) &&
        state_stats.st_nlink == 1 &&
        (state_stats.st_mode & 0077) == 0,
        "provisioning state is a private single-link file"
    );

    expect(
        ::lstat(
            credential_file.c_str(),
            &credential_stats) == 0 &&
        S_ISREG(credential_stats.st_mode) &&
        credential_stats.st_nlink == 1 &&
        (credential_stats.st_mode & 0077) == 0,
        "node credential is a private single-link file"
    );

    const std::string persisted_state =
        read_text(
            state_file
        );

    expect(
        persisted_state.find(
            first_credential) ==
                std::string::npos &&
        persisted_state.find(
            "bootstrap_token") ==
                std::string::npos &&
        persisted_state.find(
            "node_credential") ==
                std::string::npos,
        "state must not contain credentials or bootstrap tokens"
    );

    StorageNodeProvisioningStore reloaded;

    error.clear();

    expect(
        reloaded.load(
            state_file.string(),
            credential_file.string(),
            &error
        ),
        "reload prepared provisioning state: " +
            error
    );

    StorageNodeProvisioningRecord recovered;

    expect_result(
        reloaded.prepare_for_bootstrap(
            intent,
            "2026-07-31T04:01:00Z",
            &credential,
            &recovered,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            Ok,
        "recover persisted node credential"
    );

    expect(
        credential ==
            first_credential &&
        recovered.stage ==
            StorageNodeProvisioningStage::
                CredentialPersisted,
        "recovery must preserve the original credential"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &credential
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &first_credential
        );

    StorageNodeProvisioningIntent
        mismatched =
            intent;

    mismatched.expected_generation =
        8;

    expect_result(
        reloaded.prepare_for_bootstrap(
            mismatched,
            "2026-07-31T04:02:00Z",
            &credential,
            &recovered,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            StateMismatch,
        "reject a different enrollment generation"
    );

    expect(
        credential.empty(),
        "mismatched state must not expose credential"
    );

    StorageNodeProvisioningIntent
        wrong_director_tls =
            intent;

    wrong_director_tls
        .director_tls_spki_sha256_pin =
            "sha256/"
            "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

    expect_result(
        reloaded.prepare_for_bootstrap(
            wrong_director_tls,
            "2026-07-31T04:02:00Z",
            &credential,
            &recovered,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            StateMismatch,
        "reject a different Director TLS identity"
    );

    expect(
        credential.empty(),
        "Director TLS mismatch must not expose credential"
    );

    StorageNodeProvisioningRecord enrolled;

    expect_result(
        reloaded.mark_enrolled(
            intent,
            "2026-07-31T04:03:00Z",
            &enrolled,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            Ok,
        "mark exact provisioning transaction enrolled"
    );

    expect(
        enrolled.stage ==
            StorageNodeProvisioningStage::
                Enrolled &&
        enrolled.enrolled_at ==
            "2026-07-31T04:03:00Z",
        "persist enrolled state"
    );

    expect_result(
        reloaded.mark_enrolled(
            intent,
            "2026-07-31T04:04:00Z",
            &enrolled,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            Ok,
        "repeat enrolled transition idempotently"
    );

    expect(
        enrolled.enrolled_at ==
            "2026-07-31T04:03:00Z",
        "idempotent transition preserves original enrolled timestamp"
    );

    expect_result(
        reloaded.prepare_for_bootstrap(
            intent,
            "2026-07-31T04:05:00Z",
            &credential,
            &recovered,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            AlreadyEnrolled,
        "enrolled node does not re-enter bootstrap"
    );

    expect(
        credential.empty(),
        "already enrolled response must not expose credential"
    );

    const fs::path recovery_directory =
        root /
        "prepared-recovery";

    expect(
        fs::create_directory(
            recovery_directory
        ) &&
        ::chmod(
            recovery_directory.c_str(),
            0700) == 0,
        "create prepared recovery directory"
    );

    const fs::path recovery_state =
        recovery_directory /
        "state.json";

    const fs::path recovery_credential =
        recovery_directory /
        "node.credential";

    const std::string prepared_text =
        state_json(
            intent,
            "prepared",
            "2026-07-31T05:00:00Z",
            "2026-07-31T05:00:00Z"
        ).dump() +
        "\n";

    expect(
        write_private_text(
            recovery_state,
            prepared_text
        ),
        "write crash-recovery Prepared state"
    );

    StorageNodeProvisioningStore
        prepared_recovery_store;

    error.clear();

    expect(
        prepared_recovery_store.load(
            recovery_state.string(),
            recovery_credential.string(),
            &error
        ),
        "load Prepared state without credential: " +
            error
    );

    expect_result(
        prepared_recovery_store.
            prepare_for_bootstrap(
                intent,
                "2026-07-31T05:01:00Z",
                &credential,
                &recovered,
                &error
            ),
        StorageNodeProvisioningStoreResult::
            Ok,
        "recover crash before credential creation"
    );

    expect(
        pqnas::cluster::
            valid_internal_auth_credential_token(
                credential
            ) &&
        recovered.stage ==
            StorageNodeProvisioningStage::
                CredentialPersisted,
        "Prepared recovery creates one durable credential"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &credential
        );

    const fs::path persisted_recovery_directory =
        root /
        "credential-recovery";

    expect(
        fs::create_directory(
            persisted_recovery_directory
        ) &&
        ::chmod(
            persisted_recovery_directory.c_str(),
            0700) == 0,
        "create credential recovery directory"
    );

    const fs::path persisted_recovery_state =
        persisted_recovery_directory /
        "state.json";

    const fs::path persisted_recovery_credential =
        persisted_recovery_directory /
        "node.credential";

    std::string recovery_token;

    expect(
        pqnas::cluster::
            generate_internal_auth_credential_token(
                &recovery_token,
                &error
            ),
        "generate recovery token: " +
            error
    );

    expect(
        write_private_text(
            persisted_recovery_state,
            prepared_text
        ) &&
        write_private_text(
            persisted_recovery_credential,
            recovery_token + "\n"
        ),
        "write crash state after credential commit"
    );

    StorageNodeProvisioningStore
        credential_recovery_store;

    error.clear();

    expect(
        credential_recovery_store.load(
            persisted_recovery_state.string(),
            persisted_recovery_credential.string(),
            &error
        ),
        "load Prepared state with credential: " +
            error
    );

    expect_result(
        credential_recovery_store.
            prepare_for_bootstrap(
                intent,
                "2026-07-31T05:02:00Z",
                &credential,
                &recovered,
                &error
            ),
        StorageNodeProvisioningStoreResult::
            Ok,
        "recover crash after credential commit"
    );

    expect(
        credential ==
            recovery_token &&
        recovered.stage ==
            StorageNodeProvisioningStage::
                CredentialPersisted,
        "credential recovery must not rotate the credential"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &credential
        );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &recovery_token
        );

    const fs::path orphan_directory =
        root /
        "orphan";

    expect(
        fs::create_directory(
            orphan_directory
        ) &&
        ::chmod(
            orphan_directory.c_str(),
            0700) == 0,
        "create orphan credential directory"
    );

    const fs::path orphan_state =
        orphan_directory /
        "state.json";

    const fs::path orphan_credential =
        orphan_directory /
        "node.credential";

    std::string orphan_token;

    expect(
        pqnas::cluster::
            generate_internal_auth_credential_token(
                &orphan_token,
                &error
            ) &&
        write_private_text(
            orphan_credential,
            orphan_token + "\n"
        ),
        "write orphan credential"
    );

    StorageNodeProvisioningStore
        orphan_store;

    expect(
        !orphan_store.load(
            orphan_state.string(),
            orphan_credential.string(),
            &error
        ),
        "orphan credential without state fails closed"
    );

    pqnas::cluster::
        clear_internal_auth_token_text(
            &orphan_token
        );

    StorageNodeProvisioningStore
        invalid_path_store;

    expect(
        !invalid_path_store.load(
            "relative/state.json",
            "relative/node.credential",
            &error
        ),
        "relative provisioning paths rejected"
    );

    expect_result(
        store.prepare_for_bootstrap(
            intent,
            "2026-02-30T04:00:00Z",
            &credential,
            &prepared,
            &error
        ),
        StorageNodeProvisioningStoreResult::
            InvalidInput,
        "invalid calendar timestamp rejected"
    );

    const fs::path unsafe_directory =
        root /
        "unsafe";

    expect(
        fs::create_directory(
            unsafe_directory
        ) &&
        ::chmod(
            unsafe_directory.c_str(),
            0755) == 0,
        "create unsafe provisioning directory"
    );

    StorageNodeProvisioningStore
        unsafe_store;

    expect(
        !unsafe_store.load(
            (
                unsafe_directory /
                "state.json"
            ).string(),
            (
                unsafe_directory /
                "node.credential"
            ).string(),
            &error
        ),
        "group-readable provisioning directory fails closed"
    );

    std::error_code cleanup_error;

    fs::remove_all(
        root,
        cleanup_error
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " storage node provisioning store test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage node provisioning store tests passed\n";

    return 0;
}
