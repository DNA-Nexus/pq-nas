#include "cluster/node_credential_registry.h"

#include "cluster/internal_auth.h"

#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::InternalAuthCredential;
using pqnas::cluster::NodeCredentialRegistry;
using pqnas::cluster::NodeCredentialRegistryResult;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void expect_result(
    NodeCredentialRegistryResult actual,
    NodeCredentialRegistryResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                node_credential_registry_result_code(
                    expected) +
            ", got " +
            pqnas::cluster::
                node_credential_registry_result_code(
                    actual)
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-node-credential-registry-test-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );
    writable.push_back('\0');

    char* result = ::mkdtemp(writable.data());
    return result ? fs::path(result) : fs::path{};
}

bool write_all(
    int descriptor,
    const std::string& contents
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

        return false;
    }

    return true;
}

bool write_file(
    const fs::path& path,
    const std::string& contents,
    mode_t mode
) {
    const int descriptor = ::open(
        path.c_str(),
        O_WRONLY |
        O_CREAT |
        O_TRUNC |
        O_CLOEXEC |
        O_NOFOLLOW,
        mode
    );

    if (descriptor < 0) {
        return false;
    }

    const bool ok =
        write_all(descriptor, contents) &&
        ::fchmod(descriptor, mode) == 0;

    const int close_result = ::close(descriptor);
    return ok && close_result == 0;
}

bool make_private_directory(
    const fs::path& path
) {
    std::error_code error;
    fs::create_directories(path, error);

    if (error) {
        return false;
    }

    return ::chmod(path.c_str(), 0700) == 0;
}

} // namespace

int main() {
    const fs::path test_directory =
        make_test_directory();

    if (test_directory.empty()) {
        std::cerr << "FAIL: could not create test directory\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code error;
        fs::remove_all(test_directory, error);
    };

    std::string token_a(43, 'A');
    std::string token_b =
        std::string(42, 'B') + "A";

    expect(
        pqnas::cluster::
            valid_internal_auth_credential_token(
                token_a) &&
        pqnas::cluster::
            valid_internal_auth_credential_token(
                token_b),
        "synthetic credential tokens must be canonical"
    );

    InternalAuthCredential credential_a;
    InternalAuthCredential credential_b;
    std::string error;

    expect(
        pqnas::cluster::
            parse_internal_auth_credential_token(
                token_a,
                &credential_a,
                &error),
        "parse first synthetic credential: " + error
    );

    error.clear();

    expect(
        pqnas::cluster::
            parse_internal_auth_credential_token(
                token_b,
                &credential_b,
                &error),
        "parse second synthetic credential: " + error
    );

    const fs::path registry_directory =
        test_directory / "node_credentials";

    NodeCredentialRegistry registry;

    expect(
        registry.load(
            registry_directory.string(),
            &error),
        "missing credential directory should load as empty: " +
            error
    );

    bool present = true;
    error.clear();

    expect_result(
        registry.contains(
            "node-01",
            &present,
            &error),
        NodeCredentialRegistryResult::Ok,
        "missing node credential lookup"
    );

    expect(
        !present,
        "missing node credential should not be present"
    );

    expect_result(
        registry.create_if_absent(
            "bad/node",
            token_a,
            &error),
        NodeCredentialRegistryResult::InvalidInput,
        "invalid node ID must be rejected"
    );

    expect_result(
        registry.create_if_absent(
            "node-01",
            "not-a-token",
            &error),
        NodeCredentialRegistryResult::InvalidInput,
        "invalid credential token must be rejected"
    );

    error.clear();

    expect_result(
        registry.create_if_absent(
            "node-01",
            token_a,
            &error),
        NodeCredentialRegistryResult::Ok,
        "persist first node credential"
    );

    error.clear();

    expect_result(
        registry.create_if_absent(
            "node-01",
            token_b,
            &error),
        NodeCredentialRegistryResult::
            AlreadyExists,
        "create-only mutation must not replace an existing credential"
    );

    expect(
        std::string(
            pqnas::cluster::
                node_credential_registry_result_code(
                    NodeCredentialRegistryResult::
                        AlreadyExists
                )
        ) == "node_credential_already_exists",
        "stable duplicate credential reason code"
    );

    struct stat directory_stats {};
    struct stat credential_stats {};

    expect(
        ::stat(
            registry_directory.c_str(),
            &directory_stats) == 0 &&
        S_ISDIR(directory_stats.st_mode) &&
        (directory_stats.st_mode & 0077) == 0,
        "credential directory must be service-private"
    );

    const fs::path credential_path =
        registry_directory /
        "node-01.credential";

    expect(
        ::lstat(
            credential_path.c_str(),
            &credential_stats) == 0 &&
        S_ISREG(credential_stats.st_mode) &&
        credential_stats.st_nlink == 1 &&
        (credential_stats.st_mode & 0077) == 0,
        "credential file must be private regular single-link state"
    );

    present = false;
    error.clear();

    expect_result(
        registry.contains(
            "node-01",
            &present,
            &error),
        NodeCredentialRegistryResult::Ok,
        "persisted node credential lookup"
    );

    expect(
        present,
        "persisted node credential should be present"
    );

    std::string authorization;
    error.clear();

    expect_result(
        registry.make_bearer_authorization(
            "node-01",
            &authorization,
            &error),
        NodeCredentialRegistryResult::Ok,
        "build first node authorization"
    );

    expect(
        pqnas::cluster::
            verify_internal_bearer_authorization(
                authorization,
                credential_a) &&
        !pqnas::cluster::
            verify_internal_bearer_authorization(
                authorization,
                credential_b),
        "first authorization must use the first credential"
    );

    pqnas::cluster::clear_internal_auth_token_text(
        &authorization
    );

    error.clear();

    expect_result(
        registry.upsert(
            "node-01",
            token_b,
            &error),
        NodeCredentialRegistryResult::Ok,
        "rotate node credential"
    );

    expect_result(
        registry.make_bearer_authorization(
            "node-01",
            &authorization,
            &error),
        NodeCredentialRegistryResult::Ok,
        "build rotated node authorization"
    );

    expect(
        !pqnas::cluster::
            verify_internal_bearer_authorization(
                authorization,
                credential_a) &&
        pqnas::cluster::
            verify_internal_bearer_authorization(
                authorization,
                credential_b),
        "rotated authorization must use only the replacement credential"
    );

    pqnas::cluster::clear_internal_auth_token_text(
        &authorization
    );

    NodeCredentialRegistry reloaded;
    error.clear();

    expect(
        reloaded.load(
            registry_directory.string(),
            &error),
        "reload persisted credential directory: " + error
    );

    expect_result(
        reloaded.make_bearer_authorization(
            "node-01",
            &authorization,
            &error),
        NodeCredentialRegistryResult::Ok,
        "build authorization after reload"
    );

    expect(
        pqnas::cluster::
            verify_internal_bearer_authorization(
                authorization,
                credential_b),
        "reloaded registry must preserve the rotated credential"
    );

    pqnas::cluster::clear_internal_auth_token_text(
        &authorization
    );

    error.clear();

    expect_result(
        registry.upsert(
            "node-01",
            "invalid-replacement",
            &error),
        NodeCredentialRegistryResult::InvalidInput,
        "invalid rotation must fail before persistence"
    );

    expect_result(
        registry.make_bearer_authorization(
            "node-01",
            &authorization,
            &error),
        NodeCredentialRegistryResult::Ok,
        "credential must survive invalid rotation"
    );

    expect(
        pqnas::cluster::
            verify_internal_bearer_authorization(
                authorization,
                credential_b),
        "invalid rotation must preserve the active credential"
    );

    pqnas::cluster::clear_internal_auth_token_text(
        &authorization
    );

    expect_result(
        registry.erase(
            "node-01",
            &error),
        NodeCredentialRegistryResult::Ok,
        "erase node credential"
    );

    present = true;

    expect_result(
        registry.contains(
            "node-01",
            &present,
            &error),
        NodeCredentialRegistryResult::Ok,
        "lookup erased node credential"
    );

    expect(
        !present,
        "erased node credential must be absent"
    );

    expect_result(
        registry.make_bearer_authorization(
            "node-01",
            &authorization,
            &error),
        NodeCredentialRegistryResult::NotFound,
        "erased credential must not authorize"
    );

    expect_result(
        registry.erase(
            "node-01",
            &error),
        NodeCredentialRegistryResult::NotFound,
        "erasing a missing credential"
    );

    {
        const fs::path match_directory =
            test_directory /
            "match-directory";

        NodeCredentialRegistry match_registry;

        error.clear();

        expect(
            match_registry.load(
                match_directory.string(),
                &error
            ),
            "load credential match registry: " +
                error
        );

        bool matched = true;

        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                token_a,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::NotFound,
            "missing credential comparison"
        );

        expect(
            !matched,
            "missing credential comparison must clear output"
        );

        matched = true;
        error.clear();

        expect_result(
            match_registry.matches(
                "bad/node",
                token_a,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::
                InvalidInput,
            "credential comparison invalid node ID"
        );

        expect(
            !matched,
            "invalid comparison must clear output"
        );

        matched = true;
        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                "not-a-token",
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::
                InvalidInput,
            "credential comparison invalid token"
        );

        expect(
            !matched,
            "invalid token comparison must clear output"
        );

        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                token_a,
                nullptr,
                &error
            ),
            NodeCredentialRegistryResult::
                InvalidInput,
            "credential comparison requires output"
        );

        error.clear();

        expect_result(
            match_registry.create_if_absent(
                "node-match",
                token_a,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "create credential match fixture"
        );

        matched = false;
        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                token_a,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "matching persisted credential"
        );

        expect(
            matched,
            "identical credential must match"
        );

        matched = true;
        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                token_b,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "non-matching persisted credential"
        );

        expect(
            !matched,
            "different credential must not match"
        );

        error.clear();

        expect_result(
            match_registry.create_if_absent(
                "node-match",
                token_b,
                &error
            ),
            NodeCredentialRegistryResult::
                AlreadyExists,
            "duplicate create must preserve match fixture"
        );

        matched = false;
        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                token_a,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "match credential after duplicate create"
        );

        expect(
            matched,
            "duplicate create must preserve original credential"
        );

        error.clear();

        expect_result(
            match_registry.upsert(
                "node-match",
                token_b,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "rotate credential match fixture"
        );

        matched = true;
        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                token_a,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "old credential comparison after rotation"
        );

        expect(
            !matched,
            "old credential must not match after rotation"
        );

        matched = false;
        error.clear();

        expect_result(
            match_registry.matches(
                "node-match",
                token_b,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "rotated credential comparison"
        );

        expect(
            matched,
            "rotated credential must match"
        );

        NodeCredentialRegistry match_reloaded;

        error.clear();

        expect(
            match_reloaded.load(
                match_directory.string(),
                &error
            ),
            "reload credential match registry: " +
                error
        );

        matched = false;
        error.clear();

        expect_result(
            match_reloaded.matches(
                "node-match",
                token_b,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "reloaded credential comparison"
        );

        expect(
            matched,
            "matching credential must survive reload"
        );

        error.clear();

        expect_result(
            match_reloaded.erase(
                "node-match",
                &error
            ),
            NodeCredentialRegistryResult::Ok,
            "erase credential match fixture"
        );

        matched = true;
        error.clear();

        expect_result(
            match_reloaded.matches(
                "node-match",
                token_b,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::NotFound,
            "comparison after credential erase"
        );

        expect(
            !matched,
            "erased credential comparison must clear output"
        );

        NodeCredentialRegistry unloaded_match_registry;

        matched = true;
        error.clear();

        expect_result(
            unloaded_match_registry.matches(
                "node-unloaded",
                token_a,
                &matched,
                &error
            ),
            NodeCredentialRegistryResult::
                StorageUnsafe,
            "unloaded credential match registry"
        );

        expect(
            !matched,
            "unloaded comparison must clear output"
        );
    }

    const fs::path unsafe_directory =
        test_directory / "unsafe-directory";

    expect(
        make_private_directory(unsafe_directory) &&
        ::chmod(
            unsafe_directory.c_str(),
            0755) == 0,
        "create unsafe credential directory"
    );

    NodeCredentialRegistry unsafe_registry;
    error.clear();

    expect(
        !unsafe_registry.load(
            unsafe_directory.string(),
            &error),
        "group/other-accessible credential directory must fail closed"
    );

    const fs::path recovery_directory =
        test_directory / "recovery-directory";

    expect(
        make_private_directory(recovery_directory) &&
        write_file(
            recovery_directory /
                "node-recovery.credential",
            token_a + "\n",
            0600) &&
        write_file(
            recovery_directory /
                ".node-recovery.credential.tmp.123.456",
            token_b + "\n",
            0600),
        "create stale credential temp state"
    );

    NodeCredentialRegistry recovery_registry;
    error.clear();

    expect(
        recovery_registry.load(
            recovery_directory.string(),
            &error),
        "safe stale credential temp file should be removed: " +
            error
    );

    expect(
        !fs::exists(
            recovery_directory /
                ".node-recovery.credential.tmp.123.456"),
        "stale plaintext credential temp file must be removed"
    );

    expect_result(
        recovery_registry.make_bearer_authorization(
            "node-recovery",
            &authorization,
            &error),
        NodeCredentialRegistryResult::Ok,
        "registry should remain usable after temp cleanup"
    );

    expect(
        pqnas::cluster::
            verify_internal_bearer_authorization(
                authorization,
                credential_a),
        "temp cleanup must preserve committed credential"
    );

    pqnas::cluster::clear_internal_auth_token_text(
        &authorization
    );

    const fs::path unsafe_temp_directory =
        test_directory / "unsafe-temp-directory";
    const fs::path unsafe_temp_source =
        test_directory / "unsafe-temp-source";

    expect(
        make_private_directory(unsafe_temp_directory) &&
        write_file(
            unsafe_temp_source,
            token_b + "\n",
            0600),
        "create unsafe temp credential state"
    );

    std::error_code unsafe_temp_error;
    fs::create_symlink(
        unsafe_temp_source,
        unsafe_temp_directory /
            ".node-temp.credential.tmp.123.456",
        unsafe_temp_error
    );

    expect(
        !unsafe_temp_error,
        "create unsafe credential temp symlink"
    );

    NodeCredentialRegistry unsafe_temp_registry;
    error.clear();

    if (!unsafe_temp_error) {
        expect(
            !unsafe_temp_registry.load(
                unsafe_temp_directory.string(),
                &error),
            "credential temp symlink must fail closed"
        );
    }

    const fs::path malformed_directory =
        test_directory / "malformed-directory";

    expect(
        make_private_directory(malformed_directory) &&
        write_file(
            malformed_directory /
                "node-malformed.credential",
            "not-a-token\n",
            0600),
        "create malformed credential file"
    );

    NodeCredentialRegistry malformed_registry;
    error.clear();

    expect(
        !malformed_registry.load(
            malformed_directory.string(),
            &error),
        "malformed credential file must fail closed"
    );

    const fs::path symlink_directory =
        test_directory / "symlink-directory";
    const fs::path symlink_source =
        test_directory / "symlink-source";

    expect(
        make_private_directory(symlink_directory) &&
        write_file(
            symlink_source,
            token_a + "\n",
            0600),
        "create credential symlink source"
    );

    std::error_code filesystem_error;
    fs::create_symlink(
        symlink_source,
        symlink_directory /
            "node-link.credential",
        filesystem_error
    );

    expect(
        !filesystem_error,
        "create credential symlink target"
    );

    NodeCredentialRegistry symlink_registry;
    error.clear();

    if (!filesystem_error) {
        expect(
            !symlink_registry.load(
                symlink_directory.string(),
                &error),
            "credential symlink must fail closed"
        );
    }

    const fs::path hardlink_directory =
        test_directory / "hardlink-directory";
    const fs::path hardlink_source =
        test_directory / "hardlink-source";

    expect(
        make_private_directory(hardlink_directory) &&
        write_file(
            hardlink_source,
            token_a + "\n",
            0600),
        "create credential hardlink source"
    );

    filesystem_error.clear();
    fs::create_hard_link(
        hardlink_source,
        hardlink_directory /
            "node-hard.credential",
        filesystem_error
    );

    expect(
        !filesystem_error,
        "create credential hardlink target"
    );

    NodeCredentialRegistry hardlink_registry;
    error.clear();

    if (!filesystem_error) {
        expect(
            !hardlink_registry.load(
                hardlink_directory.string(),
                &error),
            "credential hardlink must fail closed"
        );
    }

    const fs::path mode_directory =
        test_directory / "mode-directory";

    expect(
        make_private_directory(mode_directory) &&
        write_file(
            mode_directory /
                "node-mode.credential",
            token_a + "\n",
            0640),
        "create group-readable credential file"
    );

    NodeCredentialRegistry mode_registry;
    error.clear();

    expect(
        !mode_registry.load(
            mode_directory.string(),
            &error),
        "group-readable credential file must fail closed"
    );

    const fs::path unknown_directory =
        test_directory / "unknown-directory";

    expect(
        make_private_directory(unknown_directory) &&
        write_file(
            unknown_directory / "README",
            "unexpected\n",
            0600),
        "create unknown credential registry entry"
    );

    NodeCredentialRegistry unknown_registry;
    error.clear();

    expect(
        !unknown_registry.load(
            unknown_directory.string(),
            &error),
        "unknown credential registry entry must fail closed"
    );

    pqnas::cluster::clear_internal_auth_token_text(
        &authorization
    );
    pqnas::cluster::clear_internal_auth_token_text(
        &token_a
    );
    pqnas::cluster::clear_internal_auth_token_text(
        &token_b
    );

    cleanup();

    if (failures != 0) {
        std::cerr
            << failures
            << " node credential registry test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node credential registry tests passed\n";
    return 0;
}
