#include "opaque_backend_status.h"
#include "runtime_paths.h"

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

bool contains_reason(const std::vector<std::string>& reasons, const std::string& needle) {
    for (const auto& reason : reasons) {
        if (reason == needle) return true;
    }
    return false;
}

bool contains_text(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::filesystem::path temp_root() {
    return std::filesystem::temp_directory_path() /
           ("pqnas_opaque_backend_status_" + std::to_string(static_cast<long long>(::getpid())));
}

void unset_opaque_env() {
    ::unsetenv("PQNAS_CONFIG_ROOT");
}

void write_file(const std::filesystem::path& p, const std::string& body) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream out(p.string(), std::ios::trunc);
    if (!out.good()) fail("failed to open " + p.string());
    out << body;
    out.flush();
    if (!out.good()) fail("failed to write " + p.string());
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    unset_opaque_env();

    const fs::path root = temp_root();
    const fs::path credentials = root / "opaque_credentials.json";
    const fs::path setup = root / "opaque_server_setup.bin";
    const fs::path helper = pqnas::opaque_helper_path();

    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    require_true(!ec, "failed to create temp root");

    require_true(::setenv("PQNAS_CONFIG_ROOT", root.string().c_str(), 1) == 0,
                 "setenv PQNAS_CONFIG_ROOT should succeed");

    auto missing = pqnas::check_opaque_backend_status();
    require_true(missing.credentials_path == credentials, "credentials path should use config root");
    require_true(missing.server_setup_path == setup, "server setup path should use config root");
    require_true(missing.helper_path == helper, "helper path should use pinned libexec path");
    require_true(!missing.ready_for_login, "missing backend must not be ready");
    require_true(contains_reason(missing.missing_or_not_ready, "opaque_credentials_missing"),
                 "missing credentials reason should be reported internally");
    require_true(contains_reason(missing.missing_or_not_ready, "opaque_server_setup_missing"),
                 "missing server setup reason should be reported internally");
    require_true(missing.helper_path == helper,
                 "missing-state diagnostic should resolve the pinned helper path");
    require_true(pqnas::opaque_backend_public_error(missing) == "opaque_backend_not_configured",
                 "public error should be generic");
    const std::string missing_diag = pqnas::opaque_backend_internal_diagnostic_json(missing);
    require_true(contains_text(missing_diag, "\"ready_for_login\":false"),
                 "internal diagnostic should report fail-closed readiness");
    require_true(contains_text(missing_diag, "opaque_credentials_missing"),
                 "internal diagnostic should include missing credential reason");
    require_true(contains_text(missing_diag, "\"credentials_path\":"),
                 "internal diagnostic should include resolved credentials path");
    require_true(contains_text(missing_diag, "\"server_setup_path\":"),
                 "internal diagnostic should include resolved server setup path");
    require_true(contains_text(missing_diag, "\"helper_path\":"),
                 "internal diagnostic should include resolved helper path");

    write_file(credentials, "{ \"version\": 1, \"accounts\": [] }\n");
    write_file(setup, "fake-server-setup-placeholder\n");
    write_file(credentials,
               "{\n"
               "  \"version\": 1,\n"
               "  \"accounts\": [\n"
               "    {\n"
               "      \"login\": \"user@example.com\",\n"
               "      \"fingerprint\": \"abcdef123456\",\n"
               "      \"opaque_password_file_b64\": \"abc\",\n"
               "      \"opaque_suite\": \"suite\",\n"
               "      \"password_hash\": \"$argon2id$must-not-exist-here\"\n"
               "    }\n"
               "  ]\n"
               "}\n");
    auto invalid_credentials = pqnas::check_opaque_backend_status();
    require_true(!invalid_credentials.credentials_store_valid,
                 "credentials store with password_hash fallback must fail closed");
    require_true(contains_reason(invalid_credentials.missing_or_not_ready, "opaque_credentials_invalid"),
                 "invalid credentials reason should be reported internally");

    write_file(credentials, "{ \"version\": 1, \"accounts\": [] }\n");

    auto configured_without_test_helper = pqnas::check_opaque_backend_status();
    require_true(configured_without_test_helper.credentials_file_exists,
                 "credentials file should exist after writing valid store");
    require_true(configured_without_test_helper.credentials_file_readable,
                 "credentials file should be readable after writing valid store");
    require_true(configured_without_test_helper.credentials_store_valid,
                 "credentials store should parse as valid");
    require_true(configured_without_test_helper.credentials_account_count == 0,
                 "empty credentials store should report zero accounts");
    require_true(configured_without_test_helper.server_setup_file_exists,
                 "server setup file should exist after writing setup");
    require_true(configured_without_test_helper.server_setup_file_readable,
                 "server setup file should be readable after writing setup");
    require_true(configured_without_test_helper.helper_path == helper,
                 "helper path should remain pinned to libexec");
    require_true(!configured_without_test_helper.ready_for_login,
                 "backend must remain fail-closed without relying on helper env overrides");

    fs::remove_all(root, ec);
    unset_opaque_env();

    std::cout << "ok: OPAQUE backend status scaffold tests passed\n";
    return 0;
}
