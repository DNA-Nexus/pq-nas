#include "cluster/storage_node_credential_runtime.h"

#include "cluster/internal_auth.h"

#include <sodium.h>

#include <array>
#include <cerrno>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

using pqnas::cluster::
    StorageNodeCredentialRuntime;

using pqnas::cluster::
    StorageNodeCredentialRuntimeResult;

int failures = 0;

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
    StorageNodeCredentialRuntimeResult actual,
    StorageNodeCredentialRuntimeResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            " actual=" +
            pqnas::cluster::
                storage_node_credential_runtime_result_code(
                    actual
                ) +
            " expected=" +
            pqnas::cluster::
                storage_node_credential_runtime_result_code(
                    expected
                )
    );
}

fs::path make_test_directory() {
    std::string pattern =
        (
            fs::temp_directory_path() /
            "pqnas-storage-credential-runtime-XXXXXX"
        ).string();

    std::vector<char> writable(
        pattern.begin(),
        pattern.end()
    );

    writable.push_back(
        '\0'
    );

    char* created =
        ::mkdtemp(
            writable.data()
        );

    return created
        ? fs::path(created)
        : fs::path{};
}

std::string synthetic_token(
    unsigned char seed
) {
    std::array<
        unsigned char,
        pqnas::cluster::
            kInternalAuthSecretBytes
    > bytes{};

    for (std::size_t index = 0;
         index < bytes.size();
         ++index) {
        bytes[index] =
            static_cast<unsigned char>(
                seed +
                static_cast<unsigned char>(
                    index
                )
            );
    }

    std::array<
        char,
        sodium_base64_ENCODED_LEN(
            pqnas::cluster::
                kInternalAuthSecretBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        )
    > encoded{};

    if (!sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            bytes.data(),
            bytes.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING)) {
        sodium_memzero(
            bytes.data(),
            bytes.size()
        );

        return {};
    }

    sodium_memzero(
        bytes.data(),
        bytes.size()
    );

    return
        std::string(
            encoded.data()
        );
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

    const std::string contents =
        text + "\n";

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

        (void)::close(
            descriptor
        );

        return false;
    }

    const bool file_synced =
        ::fsync(
            descriptor
        ) == 0;

    const bool closed =
        ::close(
            descriptor
        ) == 0;

    const int directory_descriptor =
        ::open(
            path.parent_path().c_str(),
            O_RDONLY |
            O_DIRECTORY |
            O_CLOEXEC |
            O_NOFOLLOW
        );

    bool directory_synced = false;

    if (directory_descriptor >= 0) {
        directory_synced =
            ::fsync(
                directory_descriptor
            ) == 0;

        (void)::close(
            directory_descriptor
        );
    }

    return
        file_synced &&
        closed &&
        directory_synced;
}

std::string authorization(
    const std::string& token
) {
    return
        "Bearer " +
        token;
}

std::string read_token(
    const fs::path& path
) {
    std::ifstream input(
        path
    );

    std::string value;

    std::getline(
        input,
        value
    );

    return value;
}

void cleanup(
    const fs::path& root
) {
    std::error_code error;

    fs::remove_all(
        root,
        error
    );
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";

        return 1;
    }

    const std::string old_token =
        synthetic_token(1);

    expect(
        old_token.size() ==
            pqnas::cluster::
                kInternalAuthTokenChars,
        "synthetic active credential"
    );

    // -----------------------------------------------------------------
    // OLD -> OLD+NEW -> restart -> NEW
    // -----------------------------------------------------------------

    const fs::path root =
        make_test_directory();

    if (root.empty()) {
        std::cerr
            << "FAIL: test directory creation failed\n";

        return 1;
    }

    const fs::path active =
        root /
        "cluster_node.credential";

    const fs::path pending =
        root /
        "cluster_node.credential.pending";

    expect(
        write_private_text(
            active,
            old_token
        ),
        "write active credential fixture"
    );

    std::string new_token;

    {
        StorageNodeCredentialRuntime
            runtime;

        std::string error;

        expect(
            runtime.load(
                active.string(),
                &error
            ),
            "runtime loads active credential: " +
                error
        );

        expect(
            runtime.verify_authorization(
                authorization(
                    old_token
                )
            ),
            "OLD accepted before rotation"
        );

        const std::string unknown_token =
            synthetic_token(91);

        expect(
            !runtime.verify_authorization(
                authorization(
                    unknown_token
                )
            ),
            "unknown credential rejected before rotation"
        );

        expect(
            !runtime.rotation_pending(),
            "no pending credential initially"
        );

        expect_result(
            runtime.prepare_rotation_authorized(
                authorization(
                    unknown_token
                ),
                &new_token,
                &error
            ),
            StorageNodeCredentialRuntimeResult::
                AuthenticationFailed,
            "wrong credential cannot prepare rotation"
        );

        expect(
            !runtime.rotation_pending(),
            "failed authorization creates no pending credential"
        );

        expect_result(
            runtime.prepare_rotation_authorized(
                authorization(
                    old_token
                ),
                &new_token,
                &error
            ),
            StorageNodeCredentialRuntimeResult::
                Ok,
            "prepare rotation"
        );

        expect(
            new_token.size() ==
                pqnas::cluster::
                    kInternalAuthTokenChars &&
            new_token != old_token,
            "prepare returns distinct NEW credential"
        );

        expect(
            runtime.rotation_pending(),
            "runtime reports pending rotation"
        );

        expect(
            runtime.verify_authorization(
                authorization(
                    old_token
                )
            ),
            "OLD remains accepted while rotation is pending"
        );

        expect(
            runtime.verify_authorization(
                authorization(
                    new_token
                )
            ),
            "NEW accepted while rotation is pending"
        );

        expect(
            fs::exists(
                pending
            ),
            "pending recovery file exists"
        );

        std::string retry_token;

        expect_result(
            runtime.prepare_rotation_authorized(
                authorization(
                    new_token
                ),
                &retry_token,
                &error
            ),
            StorageNodeCredentialRuntimeResult::
                Ok,
            "prepare retry is idempotent"
        );

        expect(
            retry_token == new_token,
            "prepare retry returns same pending credential"
        );

        pqnas::cluster::
            clear_internal_auth_token_text(
                &retry_token
            );
    }

    // Simulate process restart while Director/Storage rotation is pending.
    {
        StorageNodeCredentialRuntime
            recovered;

        std::string error;

        expect(
            recovered.load(
                active.string(),
                &error
            ),
            "restart recovers active + pending credentials: " +
                error
        );

        expect(
            recovered.rotation_pending(),
            "restart preserves pending state"
        );

        expect(
            recovered.verify_authorization(
                authorization(
                    old_token
                )
            ),
            "restart still accepts OLD"
        );

        expect(
            recovered.verify_authorization(
                authorization(
                    new_token
                )
            ),
            "restart still accepts NEW"
        );

        expect_result(
            recovered.finalize_rotation_authorized(
                authorization(
                    old_token
                ),
                &error
            ),
            StorageNodeCredentialRuntimeResult::
                AuthenticationFailed,
            "OLD cannot finalize pending rotation"
        );

        expect(
            recovered.rotation_pending() &&
            recovered.verify_authorization(
                authorization(
                    old_token
                )
            ) &&
            recovered.verify_authorization(
                authorization(
                    new_token
                )
            ),
            "failed OLD finalize leaves overlap state intact"
        );

        expect_result(
            recovered.finalize_rotation_authorized(
                authorization(
                    new_token
                ),
                &error
            ),
            StorageNodeCredentialRuntimeResult::
                Ok,
            "NEW finalizes rotation"
        );

        expect(
            !recovered.rotation_pending(),
            "pending state cleared after finalize"
        );

        expect(
            !recovered.verify_authorization(
                authorization(
                    old_token
                )
            ),
            "OLD rejected after durable finalize"
        );

        expect(
            recovered.verify_authorization(
                authorization(
                    new_token
                )
            ),
            "NEW accepted after durable finalize"
        );

        expect(
            !fs::exists(
                pending
            ),
            "pending recovery file removed after finalize"
        );

        expect(
            read_token(
                active
            ) == new_token,
            "active credential file contains NEW after finalize"
        );

        expect_result(
            recovered.finalize_rotation_authorized(
                authorization(
                    new_token
                ),
                &error
            ),
            StorageNodeCredentialRuntimeResult::
                NoPending,
            "ACTIVE finalize retry after completion reports no pending"
        );
    }

    // Restart after completed finalize must accept NEW only.
    {
        StorageNodeCredentialRuntime
            finalized;

        std::string error;

        expect(
            finalized.load(
                active.string(),
                &error
            ),
            "restart after finalize loads NEW: " +
                error
        );

        expect(
            !finalized.rotation_pending(),
            "completed rotation stays completed after restart"
        );

        expect(
            !finalized.verify_authorization(
                authorization(
                    old_token
                )
            ),
            "completed restart rejects OLD"
        );

        expect(
            finalized.verify_authorization(
                authorization(
                    new_token
                )
            ),
            "completed restart accepts NEW"
        );
    }

    cleanup(
        root
    );

    // -----------------------------------------------------------------
    // Restart after ACTIVE replacement but before PENDING cleanup.
    //
    // This represents the crash-recovery state where rename(NEW -> ACTIVE)
    // was already visible while the pending recovery anchor still exists.
    // Both files therefore contain NEW. load() must recover that state and
    // finalize safely without bringing OLD back.
    // -----------------------------------------------------------------

    {
        const fs::path promoted_root =
            make_test_directory();

        const fs::path promoted_active =
            promoted_root /
            "cluster_node.credential";

        const fs::path promoted_pending =
            promoted_root /
            "cluster_node.credential.pending";

        const std::string promoted_token =
            synthetic_token(44);

        expect(
            write_private_text(
                promoted_active,
                promoted_token
            ),
            "write promoted active recovery fixture"
        );

        expect(
            write_private_text(
                promoted_pending,
                promoted_token
            ),
            "write promoted pending recovery fixture"
        );

        StorageNodeCredentialRuntime
            recovered;

        std::string error;

        expect(
            recovered.load(
                promoted_active.string(),
                &error
            ),
            "restart recovers ACTIVE == PENDING state: " +
                error
        );

        expect(
            recovered.rotation_pending(),
            "promoted recovery retains pending anchor until finalize"
        );

        expect(
            recovered.verify_authorization(
                authorization(
                    promoted_token
                )
            ),
            "promoted NEW credential is accepted"
        );

        expect(
            !recovered.verify_authorization(
                authorization(
                    old_token
                )
            ),
            "promoted recovery does not resurrect OLD"
        );

        expect_result(
            recovered.finalize_rotation_authorized(
                authorization(
                    promoted_token
                ),
                &error
            ),
            StorageNodeCredentialRuntimeResult::
                Ok,
            "promoted recovery finalizes with NEW"
        );

        expect(
            !fs::exists(
                promoted_pending
            ),
            "promoted recovery removes pending anchor"
        );

        expect(
            read_token(
                promoted_active
            ) == promoted_token,
            "promoted recovery keeps NEW active"
        );

        cleanup(
            promoted_root
        );
    }

    // -----------------------------------------------------------------
    // Unsafe pending symlink fails closed.
    // -----------------------------------------------------------------

    {
        const fs::path unsafe_root =
            make_test_directory();

        const fs::path unsafe_active =
            unsafe_root /
            "cluster_node.credential";

        const fs::path unsafe_pending =
            unsafe_root /
            "cluster_node.credential.pending";

        expect(
            write_private_text(
                unsafe_active,
                old_token
            ),
            "write symlink fixture active credential"
        );

        std::error_code link_error;

        fs::create_symlink(
            unsafe_active,
            unsafe_pending,
            link_error
        );

        expect(
            !link_error,
            "create pending symlink fixture"
        );

        StorageNodeCredentialRuntime
            runtime;

        std::string error;

        expect(
            !runtime.load(
                unsafe_active.string(),
                &error
            ),
            "pending symlink fails closed"
        );

        expect(
            !error.empty(),
            "pending symlink reports failure category"
        );

        cleanup(
            unsafe_root
        );
    }

    // -----------------------------------------------------------------
    // Malformed pending credential fails closed.
    // -----------------------------------------------------------------

    {
        const fs::path invalid_root =
            make_test_directory();

        const fs::path invalid_active =
            invalid_root /
            "cluster_node.credential";

        const fs::path invalid_pending =
            invalid_root /
            "cluster_node.credential.pending";

        expect(
            write_private_text(
                invalid_active,
                old_token
            ),
            "write malformed-pending active fixture"
        );

        expect(
            write_private_text(
                invalid_pending,
                "not-a-valid-token"
            ),
            "write malformed pending fixture"
        );

        StorageNodeCredentialRuntime
            runtime;

        std::string error;

        expect(
            !runtime.load(
                invalid_active.string(),
                &error
            ),
            "malformed pending credential fails closed"
        );

        cleanup(
            invalid_root
        );
    }

    // -----------------------------------------------------------------
    // Group/other-accessible credential directory fails closed.
    // -----------------------------------------------------------------

    {
        const fs::path mode_root =
            make_test_directory();

        const fs::path mode_active =
            mode_root /
            "cluster_node.credential";

        expect(
            write_private_text(
                mode_active,
                old_token
            ),
            "write unsafe-directory active fixture"
        );

        expect(
            ::chmod(
                mode_root.c_str(),
                0755
            ) == 0,
            "make credential directory intentionally unsafe"
        );

        StorageNodeCredentialRuntime
            runtime;

        std::string error;

        expect(
            !runtime.load(
                mode_active.string(),
                &error
            ),
            "group/other-accessible directory fails closed"
        );

        (void)::chmod(
            mode_root.c_str(),
            0700
        );

        cleanup(
            mode_root
        );
    }

    // -----------------------------------------------------------------
    // Hard-linked credential state fails closed.
    // -----------------------------------------------------------------

    {
        const fs::path link_root =
            make_test_directory();

        const fs::path link_active =
            link_root /
            "cluster_node.credential";

        const fs::path link_pending =
            link_root /
            "cluster_node.credential.pending";

        expect(
            write_private_text(
                link_active,
                old_token
            ),
            "write hard-link active fixture"
        );

        std::error_code link_error;

        fs::create_hard_link(
            link_active,
            link_pending,
            link_error
        );

        expect(
            !link_error,
            "create hard-link fixture"
        );

        StorageNodeCredentialRuntime
            runtime;

        std::string error;

        expect(
            !runtime.load(
                link_active.string(),
                &error
            ),
            "hard-linked credential state fails closed"
        );

        cleanup(
            link_root
        );
    }

    // -----------------------------------------------------------------
    // Safe stale temporary files are recovery debris, not authority.
    // -----------------------------------------------------------------

    {
        const fs::path stale_root =
            make_test_directory();

        const fs::path stale_active =
            stale_root /
            "cluster_node.credential";

        const fs::path stale_temp =
            stale_root /
            "cluster_node.credential.rotate.tmp";

        expect(
            write_private_text(
                stale_active,
                old_token
            ),
            "write stale-temp active fixture"
        );

        expect(
            write_private_text(
                stale_temp,
                "partial"
            ),
            "write safe stale temporary fixture"
        );

        StorageNodeCredentialRuntime
            runtime;

        std::string error;

        expect(
            runtime.load(
                stale_active.string(),
                &error
            ),
            "safe stale temporary file is cleaned: " +
                error
        );

        expect(
            !fs::exists(
                stale_temp
            ),
            "stale temporary file removed durably"
        );

        expect(
            runtime.verify_authorization(
                authorization(
                    old_token
                )
            ),
            "stale temporary cleanup preserves active credential"
        );

        cleanup(
            stale_root
        );
    }

    pqnas::cluster::
        clear_internal_auth_token_text(
            &new_token
        );

    if (failures == 0) {
        std::cout
            << "OK: storage node credential runtime tests passed\n";

        return 0;
    }

    std::cerr
        << "FAIL: "
        << failures
        << " storage node credential runtime test(s) failed\n";

    return 1;
}
