#include "opaque_credentials.h"
#include "password_credentials.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

namespace fs = std::filesystem;

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

pqnas::OpaqueCredentialRec make_opaque(
    const std::string& login,
    const std::string& fingerprint
) {
    pqnas::OpaqueCredentialRec rec;
    rec.login = login;
    rec.fingerprint = fingerprint;
    rec.opaque_password_file_b64 = "test-password-file";
    rec.opaque_suite = "test-suite";
    rec.enabled = true;
    return rec;
}

pqnas::PasswordCredentialRec make_password(
    const std::string& login,
    const std::string& fingerprint
) {
    pqnas::PasswordCredentialRec rec;
    rec.login = login;
    rec.fingerprint = fingerprint;
    rec.password_hash = "test-nonempty-hash";
    rec.enabled = true;
    return rec;
}

} // namespace

int main() {
    const fs::path dir =
        fs::temp_directory_path() /
        ("pqnas-auth-fp-erase-" + std::to_string(::getpid()));

    std::error_code ec;
    fs::remove_all(dir, ec);
    ec.clear();

    if (!fs::create_directories(dir, ec) || ec) {
        std::cerr << "FAIL: temp directory creation failed\n";
        return 1;
    }

    const fs::path opaque_path =
        dir / "opaque_credentials.json";

    const fs::path password_path =
        dir / "password_credentials.json";

    const std::string target_fp = "fingerprint-target";
    const std::string other_fp = "fingerprint-other";

    {
        pqnas::OpaqueCredentials creds;

        expect(
            creds.upsert(make_opaque("first@example.test", target_fp)),
            "opaque first alias insert"
        );

        expect(
            creds.upsert(make_opaque("second@example.test", target_fp)),
            "opaque second alias insert"
        );

        expect(
            creds.upsert(make_opaque("other@example.test", other_fp)),
            "opaque unrelated insert"
        );

        expect(
            creds.save(opaque_path.string()),
            "opaque initial save"
        );
    }

    {
        pqnas::OpaqueCredentials creds;

        expect(
            creds.load(opaque_path.string()),
            "opaque reload before erase"
        );

        expect(
            creds.erase_for_fingerprint(target_fp) == 2,
            "opaque fingerprint erase must remove every target alias"
        );

        expect(
            creds.get("first@example.test").has_value() == false,
            "opaque first target alias removed"
        );

        expect(
            creds.get("second@example.test").has_value() == false,
            "opaque second target alias removed"
        );

        expect(
            creds.get("other@example.test").has_value(),
            "opaque unrelated credential preserved"
        );

        expect(
            creds.erase_for_fingerprint(target_fp) == 0,
            "opaque repeated fingerprint erase must be idempotent"
        );

        expect(
            creds.save(opaque_path.string()),
            "opaque save after erase"
        );
    }

    {
        pqnas::OpaqueCredentials creds;

        expect(
            creds.load(opaque_path.string()),
            "opaque reload after erase"
        );

        expect(
            !creds.get("first@example.test").has_value() &&
            !creds.get("second@example.test").has_value(),
            "opaque target credentials stay deleted after reload"
        );

        expect(
            creds.get("other@example.test").has_value(),
            "opaque unrelated credential survives reload"
        );
    }

    {
        pqnas::PasswordCredentials creds;

        expect(
            creds.upsert(make_password("first@example.test", target_fp)),
            "password first alias insert"
        );

        expect(
            creds.upsert(make_password("second@example.test", target_fp)),
            "password second alias insert"
        );

        expect(
            creds.upsert(make_password("other@example.test", other_fp)),
            "password unrelated insert"
        );

        expect(
            creds.save(password_path.string()),
            "password initial save"
        );
    }

    {
        pqnas::PasswordCredentials creds;

        expect(
            creds.load(password_path.string()),
            "password reload before erase"
        );

        expect(
            creds.erase_for_fingerprint(target_fp) == 2,
            "password fingerprint erase must remove every target alias"
        );

        expect(
            !creds.get("first@example.test").has_value() &&
            !creds.get("second@example.test").has_value(),
            "password target aliases removed"
        );

        expect(
            creds.get("other@example.test").has_value(),
            "password unrelated credential preserved"
        );

        expect(
            creds.erase_for_fingerprint(target_fp) == 0,
            "password repeated fingerprint erase must be idempotent"
        );

        expect(
            creds.save(password_path.string()),
            "password save after erase"
        );
    }

    {
        pqnas::PasswordCredentials creds;

        expect(
            creds.load(password_path.string()),
            "password reload after erase"
        );

        expect(
            !creds.get("first@example.test").has_value() &&
            !creds.get("second@example.test").has_value(),
            "password target credentials stay deleted after reload"
        );

        expect(
            creds.get("other@example.test").has_value(),
            "password unrelated credential survives reload"
        );
    }

    fs::remove_all(dir, ec);

    if (failures != 0) {
        std::cerr
            << "FAIL: credential fingerprint erase failures="
            << failures
            << '\n';
        return 1;
    }

    std::cout
        << "OK: credential fingerprint erase regression tests passed\n";

    return 0;
}
