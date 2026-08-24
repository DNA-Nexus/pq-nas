#include "cluster/node_tls_trust_store.h"

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#if defined(__linux__)

// Test-only durability fault injection.
//
// Security: this uses the linker --wrap boundary only in the regression test.
// Production code contains no runtime fault-injection switch.
bool g_fail_next_directory_fsync = false;
int g_injected_directory_fsync_failures = 0;

extern "C" int __real_fsync(int descriptor);

extern "C" int __wrap_fsync(int descriptor) {
    struct stat stats {};

    if (g_fail_next_directory_fsync &&
        ::fstat(descriptor, &stats) == 0 &&
        S_ISDIR(stats.st_mode)) {
        g_fail_next_directory_fsync = false;
        ++g_injected_directory_fsync_failures;
        errno = EIO;
        return -1;
    }

    return __real_fsync(descriptor);
}

#endif

namespace {

bool expect(
    bool condition,
    const std::string& label
) {
    if (condition) {
        return true;
    }

    std::cerr
        << "FAILED: "
        << label
        << '\n';

    return false;
}

std::filesystem::path make_temp_directory() {
    std::string pattern =
        "/tmp/pqnas-node-tls-trust-test-XXXXXX";

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back('\0');

    char* created =
        ::mkdtemp(
            writable.data()
        );

    if (!created) {
        return {};
    }

    return std::filesystem::path(
        created
    );
}

} // namespace

int main() {
    using namespace pqnas::cluster;

    constexpr char kPinA[] =
        "sha256/"
        "lAhFeu/Qcc7BJ8H5hTmTCGGtG6lMlA25dclywJ/Gi2g=";

    constexpr char kPinB[] =
        "sha256/"
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=";

    int failures = 0;

    const auto check =
        [&failures](
            bool condition,
            const std::string& label
        ) {
            if (!expect(
                    condition,
                    label)) {
                ++failures;
            }
        };

    const std::filesystem::path root =
        make_temp_directory();

    check(
        !root.empty(),
        "create temporary test directory"
    );

    if (root.empty()) {
        return 1;
    }

    const std::filesystem::path store_path =
        root / "node_tls_trust";

    std::string error;

    NodeTlsTrustStore store;

    check(
        store.load(
            store_path.string(),
            &error
        ),
        "load missing trust store: " + error
    );

    std::string pin;

    check(
        store.get(
            "storage-test-01",
            &pin,
            &error
        ) ==
            NodeTlsTrustStoreResult::NotFound,
        "missing node must return NotFound"
    );

    check(
        store.bind_if_absent(
            "",
            kPinA,
            &error
        ) ==
            NodeTlsTrustStoreResult::InvalidInput,
        "reject invalid node ID"
    );

    check(
        store.bind_if_absent(
            "storage-test-01",
            "sha256/AAAA",
            &error
        ) ==
            NodeTlsTrustStoreResult::PinInvalid,
        "reject malformed TLS pin"
    );

    check(
        store.bind_if_absent(
            "storage-test-01",
            std::string(" ") + kPinA,
            &error
        ) ==
            NodeTlsTrustStoreResult::PinInvalid,
        "reject non-canonical TLS pin"
    );

    error.clear();

    check(
        store.bind_if_absent(
            "storage-test-01",
            kPinA,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "bind first TLS identity: " + error
    );

    pin.clear();
    error.clear();

    check(
        store.get(
            "storage-test-01",
            &pin,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "read first TLS binding: " + error
    );

    check(
        pin == kPinA,
        "first TLS binding must equal pin A"
    );

    error.clear();

    check(
        store.bind_if_absent(
            "storage-test-01",
            kPinA,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "same TLS binding must be idempotent: " +
            error
    );

    error.clear();

    check(
        store.bind_if_absent(
            "storage-test-01",
            kPinB,
            &error
        ) ==
            NodeTlsTrustStoreResult::TrustConflict,
        "different TLS identity must fail closed"
    );

    pin.clear();
    error.clear();

    check(
        store.get(
            "storage-test-01",
            &pin,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok &&
        pin == kPinA,
        "conflicting bind must not replace pin A"
    );

    // ---------------------------------------------------------------------
    // Deletion lifecycle regression
    // ---------------------------------------------------------------------

    error.clear();

    check(
        store.erase(
            "",
            &error
        ) ==
            NodeTlsTrustStoreResult::InvalidInput,
        "reject invalid TLS trust deletion node ID"
    );

    error.clear();

    check(
        store.erase(
            "storage-test-01",
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "erase TLS trust binding: " + error
    );

    pin.clear();
    error.clear();

    check(
        store.get(
            "storage-test-01",
            &pin,
            &error
        ) ==
            NodeTlsTrustStoreResult::NotFound,
        "erased TLS trust binding must be absent"
    );

    error.clear();

    check(
        store.erase(
            "storage-test-01",
            &error
        ) ==
            NodeTlsTrustStoreResult::NotFound,
        "repeated TLS trust deletion must be idempotent"
    );

    error.clear();

    check(
        store.bind_if_absent(
            "storage-test-01",
            kPinB,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "deleted Node ID may bind a new TLS identity: " + error
    );

    // ---------------------------------------------------------------------
    // Durability regression
    // ---------------------------------------------------------------------

    constexpr char kDurabilityNode[] =
        "storage-durability";

#if defined(__linux__)

    const int failures_before =
        g_injected_directory_fsync_failures;

    g_fail_next_directory_fsync = true;

    error.clear();

    const NodeTlsTrustStoreResult
        uncertain_result =
            store.bind_if_absent(
                kDurabilityNode,
                kPinA,
                &error
            );

    check(
        uncertain_result ==
            NodeTlsTrustStoreResult::PersistFailed,
        "injected parent fsync failure must report PersistFailed"
    );

    check(
        !g_fail_next_directory_fsync &&
        g_injected_directory_fsync_failures ==
            failures_before + 1,
        "exactly one directory fsync failure must be injected"
    );

    // Security regression:
    //
    // The bind file was already visible before parent fsync failed. Retrying
    // the same identity must recover that exact visible binding by syncing its
    // directory. It must never delete or replace the visible identity.
    error.clear();

    check(
        store.bind_if_absent(
            kDurabilityNode,
            kPinA,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "recover visible same TLS identity after fsync failure: " +
            error
    );

#else

    error.clear();

    check(
        store.bind_if_absent(
            kDurabilityNode,
            kPinA,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "bind durability fixture"
    );

#endif

    pin.clear();
    error.clear();

    check(
        store.get(
            kDurabilityNode,
            &pin,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok &&
        pin == kPinA,
        "durability-recovered pin A must be authoritative"
    );

    error.clear();

    check(
        store.bind_if_absent(
            kDurabilityNode,
            kPinB,
            &error
        ) ==
            NodeTlsTrustStoreResult::TrustConflict,
        "durability recovery must not permit replacement with pin B"
    );

    // Fresh object proves that success did not depend only on in-process state.
    NodeTlsTrustStore reloaded;

    error.clear();

    check(
        reloaded.load(
            store_path.string(),
            &error
        ),
        "reload TLS trust store: " + error
    );

    pin.clear();
    error.clear();

    check(
        reloaded.get(
            kDurabilityNode,
            &pin,
            &error
        ) ==
            NodeTlsTrustStoreResult::Ok,
        "read durability binding after reload: " +
            error
    );

    check(
        pin == kPinA,
        "durability-recovered pin A must survive reload"
    );

    error.clear();

    check(
        reloaded.bind_if_absent(
            kDurabilityNode,
            kPinB,
            &error
        ) ==
            NodeTlsTrustStoreResult::TrustConflict,
        "reloaded store must still reject conflicting pin B"
    );

    // Existing malformed files must make load fail closed.
    const std::filesystem::path malformed_root =
        root / "malformed";

    std::error_code filesystem_error;

    std::filesystem::create_directory(
        malformed_root,
        filesystem_error
    );

    check(
        !filesystem_error,
        "create malformed fixture directory"
    );

    if (!filesystem_error) {
        check(
            ::chmod(
                malformed_root.c_str(),
                0700
            ) == 0,
            "secure malformed fixture directory"
        );

        const std::filesystem::path malformed_file =
            malformed_root /
            "storage-bad.tls-spki";

        {
            FILE* file =
                std::fopen(
                    malformed_file.c_str(),
                    "wb"
                );

            check(
                file != nullptr,
                "create malformed trust entry"
            );

            if (file) {
                const std::string malformed =
                    "sha256/AAAA\n";

                const std::size_t written =
                    std::fwrite(
                        malformed.data(),
                        1,
                        malformed.size(),
                        file
                    );

                check(
                    written ==
                        malformed.size(),
                    "write malformed trust entry"
                );

                check(
                    std::fclose(file) == 0,
                    "close malformed trust entry"
                );

                check(
                    ::chmod(
                        malformed_file.c_str(),
                        0600
                    ) == 0,
                    "secure malformed trust entry"
                );
            }
        }

        NodeTlsTrustStore malformed_store;

        error.clear();

        check(
            !malformed_store.load(
                malformed_root.string(),
                &error
            ),
            "malformed persisted pin must fail store load"
        );
    }

    std::filesystem::remove_all(
        root,
        filesystem_error
    );

    if (failures == 0) {
        std::cout
            << "node TLS trust store tests passed\n";
    }

    return failures == 0
        ? 0
        : 1;
}
