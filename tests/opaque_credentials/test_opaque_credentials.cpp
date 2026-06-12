#include "opaque_credentials.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

void fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

void require_true(bool ok, const std::string& msg) {
    if (!ok) fail(msg);
}

std::filesystem::path temp_path(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           ("pqnas_" + name + "_" + std::to_string(static_cast<long long>(::getpid())) + ".json");
}

} // namespace

int main() {
    using pqnas::OpaqueCredentialRec;
    using pqnas::OpaqueCredentials;

    const auto path = temp_path("opaque_credentials_test");
    const auto bad_path = temp_path("opaque_credentials_bad_test");

    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(bad_path, ec);

    OpaqueCredentials store;
    require_true(store.load(path.string()), "missing file should load as empty store");
    require_true(store.size() == 0, "missing file should produce empty store");

    OpaqueCredentialRec rec;
    rec.login = "  USER@Example.COM ";
    rec.fingerprint = "abcdef123456";
    rec.opaque_password_file_b64 = "ZmFrZS1vcGFxdWUtcGFzc3dvcmQtZmlsZQ==";
    rec.opaque_suite = "opaque-ke-v1-ristretto255-3dh-sha512-argon2id";
    rec.enabled = true;
    rec.temporary = false;
    rec.created_at = "2026-06-12T00:00:00Z";
    rec.updated_at = "2026-06-12T00:00:00Z";

    require_true(store.upsert(rec), "valid record should upsert");
    require_true(store.size() == 1, "store should contain one record");

    const auto got = store.get("user@example.com");
    require_true(got.has_value(), "normalized lookup should find record");
    require_true(got->login == "user@example.com", "login should be normalized");
    require_true(got->fingerprint == rec.fingerprint, "fingerprint should round-trip");
    require_true(got->opaque_password_file_b64 == rec.opaque_password_file_b64, "OPAQUE password file should round-trip");
    require_true(got->opaque_suite == rec.opaque_suite, "OPAQUE suite should round-trip");

    require_true(store.save(path.string()), "save should succeed");

    struct stat st {};
    require_true(::stat(path.string().c_str(), &st) == 0, "saved file should exist");
    require_true((st.st_mode & (S_IRWXG | S_IRWXO)) == 0, "saved file should not grant group/other permissions");

    OpaqueCredentials loaded;
    require_true(loaded.load(path.string()), "saved file should load");
    require_true(loaded.size() == 1, "loaded store should contain one record");

    const auto loaded_rec = loaded.get("USER@example.COM");
    require_true(loaded_rec.has_value(), "loaded store should support normalized lookup");
    require_true(loaded_rec->login == "user@example.com", "loaded login should remain normalized");

    require_true(loaded.erase("USER@example.COM"), "erase should remove record");
    require_true(loaded.size() == 0, "store should be empty after erase");

    {
        std::ofstream bad(bad_path.string(), std::ios::trunc);
        bad
            << "{\n"
            << "  \"version\": 1,\n"
            << "  \"accounts\": [\n"
            << "    {\n"
            << "      \"login\": \"user@example.com\",\n"
            << "      \"fingerprint\": \"abcdef123456\",\n"
            << "      \"opaque_password_file_b64\": \"abc\",\n"
            << "      \"opaque_suite\": \"suite\",\n"
            << "      \"password_hash\": \"$argon2id$must-not-exist-here\"\n"
            << "    }\n"
            << "  ]\n"
            << "}\n";
    }

    OpaqueCredentials bad_loaded;
    require_true(!bad_loaded.load(bad_path.string()), "classic password_hash fallback field must fail closed");

    std::filesystem::remove(path, ec);
    std::filesystem::remove(bad_path, ec);

    std::cout << "ok: OpaqueCredentials scaffold tests passed\n";
    return 0;
}
