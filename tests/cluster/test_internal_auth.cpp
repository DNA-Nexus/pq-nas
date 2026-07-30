#include "cluster/internal_auth.h"

#include <sodium.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void fail(const std::string& message) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message);
    }
}

bool write_all(int fd, const std::string& value) {
    std::size_t offset = 0;

    while (offset < value.size()) {
        const ssize_t count = ::write(
            fd,
            value.data() + offset,
            value.size() - offset
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
    const std::string& value,
    mode_t mode
) {
    const int fd = ::open(
        path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
        mode
    );

    if (fd < 0) {
        return false;
    }

    const bool ok = write_all(fd, value);
    const int close_rc = ::close(fd);
    return ok && close_rc == 0;
}

fs::path make_test_directory() {
    std::string pattern =
        (fs::temp_directory_path() /
         "pqnas-internal-auth-test-XXXXXX").string();

    std::vector<char> writable(pattern.begin(), pattern.end());
    writable.push_back('\0');

    char* result = ::mkdtemp(writable.data());
    if (!result) {
        return {};
    }

    return fs::path(result);
}

std::string synthetic_token() {
    std::array<unsigned char, pqnas::cluster::kInternalAuthSecretBytes> bytes{};

    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<unsigned char>(i);
    }

    std::array<
        char,
        sodium_base64_ENCODED_LEN(
            pqnas::cluster::kInternalAuthSecretBytes,
            sodium_base64_VARIANT_URLSAFE_NO_PADDING
        )
    > encoded{};

    if (!sodium_bin2base64(
            encoded.data(),
            encoded.size(),
            bytes.data(),
            bytes.size(),
            sodium_base64_VARIANT_URLSAFE_NO_PADDING)) {
        return {};
    }

    sodium_memzero(bytes.data(), bytes.size());
    return std::string(encoded.data());
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr << "FAIL: sodium_init failed\n";
        return 1;
    }

    const fs::path test_dir = make_test_directory();
    if (test_dir.empty()) {
        std::cerr << "FAIL: could not create synthetic test directory\n";
        return 1;
    }

    const auto cleanup = [&]() {
        std::error_code ec;
        fs::remove_all(test_dir, ec);
    };

    const std::string token = synthetic_token();
    expect(
        token.size() == pqnas::cluster::kInternalAuthTokenChars,
        "synthetic token length"
    );

    const fs::path valid_path = test_dir / "node-token";
    expect(
        write_file(valid_path, token + "\n", 0600),
        "write valid credential file"
    );

    pqnas::cluster::InternalAuthCredential credential;
    std::string err;

    expect(
        pqnas::cluster::load_internal_auth_credential_file(
            valid_path.string(),
            &credential,
            &err
        ),
        "load valid credential: " + err
    );

    expect(credential.configured(), "credential should be configured");

    expect(
        pqnas::cluster::verify_internal_bearer_authorization(
            "Bearer " + token,
            credential
        ),
        "correct bearer token should authenticate"
    );

    std::string wrong_token = token;
    if (!wrong_token.empty()) {
        wrong_token[0] = wrong_token[0] == 'A' ? 'B' : 'A';
    }

    expect(
        !pqnas::cluster::verify_internal_bearer_authorization(
            "Bearer " + wrong_token,
            credential
        ),
        "wrong bearer token must be rejected"
    );

    expect(
        !pqnas::cluster::verify_internal_bearer_authorization(
            "Basic " + token,
            credential
        ),
        "wrong authorization scheme must be rejected"
    );

    expect(
        !pqnas::cluster::verify_internal_bearer_authorization(
            "Bearer  " + token,
            credential
        ),
        "extra bearer whitespace must be rejected"
    );

    expect(
        ::chmod(valid_path.c_str(), 0644) == 0,
        "make credential permissions unsafe"
    );

    pqnas::cluster::InternalAuthCredential unsafe_permissions;
    err.clear();

    expect(
        !pqnas::cluster::load_internal_auth_credential_file(
            valid_path.string(),
            &unsafe_permissions,
            &err
        ),
        "group/other-readable credential must be rejected"
    );

    expect(
        !unsafe_permissions.configured(),
        "rejected credential must remain unconfigured"
    );

    expect(
        ::chmod(valid_path.c_str(), 0600) == 0,
        "restore credential permissions"
    );

    const fs::path symlink_path = test_dir / "node-token-link";
    std::error_code symlink_ec;
    fs::create_symlink(valid_path.filename(), symlink_path, symlink_ec);

    expect(!symlink_ec, "create credential symlink");

    if (!symlink_ec) {
        pqnas::cluster::InternalAuthCredential symlink_credential;
        err.clear();

        expect(
            !pqnas::cluster::load_internal_auth_credential_file(
                symlink_path.string(),
                &symlink_credential,
                &err
            ),
            "credential symlink must be rejected"
        );
    }

    const fs::path malformed_path = test_dir / "malformed-token";
    expect(
        write_file(malformed_path, "not-a-valid-token\n", 0600),
        "write malformed credential file"
    );

    pqnas::cluster::InternalAuthCredential malformed;
    err.clear();

    expect(
        !pqnas::cluster::load_internal_auth_credential_file(
            malformed_path.string(),
            &malformed,
            &err
        ),
        "malformed credential must be rejected"
    );

    std::string outbound_authorization;

    expect(
        pqnas::cluster::make_internal_bearer_authorization(
            credential,
            &outbound_authorization
        ),
        "configured credential should build outbound bearer authorization"
    );

    expect(
        outbound_authorization == "Bearer " + token,
        "outbound bearer authorization should match loaded token"
    );

    outbound_authorization.clear();
    credential.clear();

    expect(
        !pqnas::cluster::make_internal_bearer_authorization(
            credential,
            &outbound_authorization
        ) &&
        outbound_authorization.empty(),
        "cleared credential must not build outbound authorization"
    );

    expect(
        !pqnas::cluster::verify_internal_bearer_authorization(
            "Bearer " + token,
            credential
        ),
        "cleared credential must reject authentication"
    );

    cleanup();

    if (failures != 0) {
        std::cerr << failures << " internal auth test(s) failed\n";
        return 1;
    }

    std::cout << "OK: internal auth credential tests passed\n";
    return 0;
}
