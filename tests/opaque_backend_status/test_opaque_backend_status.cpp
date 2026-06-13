#include "opaque_backend_status.h"

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
    ::unsetenv("PQNAS_OPAQUE_CREDENTIALS_PATH");
    ::unsetenv("PQNAS_OPAQUE_SERVER_SETUP_PATH");
    ::unsetenv("PQNAS_OPAQUE_HELPER");
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
    const fs::path helper = root / "pqnas_opaque_helper";

    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    require_true(!ec, "failed to create temp root");

    require_true(::setenv("PQNAS_CONFIG_ROOT", root.string().c_str(), 1) == 0,
                 "setenv PQNAS_CONFIG_ROOT should succeed");
    require_true(::setenv("PQNAS_OPAQUE_HELPER", helper.string().c_str(), 1) == 0,
                 "setenv PQNAS_OPAQUE_HELPER should succeed");

    auto missing = pqnas::check_opaque_backend_status();
    require_true(missing.credentials_path == credentials, "credentials path should use config root");
    require_true(missing.server_setup_path == setup, "server setup path should use config root");
    require_true(missing.helper_path == helper, "helper path should use explicit override");
    require_true(!missing.ready_for_login, "missing backend must not be ready");
    require_true(contains_reason(missing.missing_or_not_ready, "opaque_credentials_missing"),
                 "missing credentials reason should be reported internally");
    require_true(contains_reason(missing.missing_or_not_ready, "opaque_server_setup_missing"),
                 "missing server setup reason should be reported internally");
    require_true(contains_reason(missing.missing_or_not_ready, "opaque_helper_missing"),
                 "missing helper reason should be reported internally");
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

    write_file(helper,
               "#!/bin/sh\n"
               "if [ \"$1\" = \"--version\" ]; then\n"
               "  echo \"pqnas_opaque_helper test-scaffold\"\n"
               "  exit 0\n"
               "fi\n"
               "if [ \"$1\" = \"self-test\" ]; then\n"
               "  echo \"ok: pqnas_opaque_helper scaffold self-test passed\"\n"
               "  exit 0\n"
               "fi\n"
               "if [ \"$1\" = \"server-setup-check\" ]; then\n"
               "  echo '{\"ok\":true,\"op\":\"server-setup-check\",\"bytes_read\":27}'\n"
               "  exit 0\n"
               "fi\n"
               "exit 9\n");
    require_true(::chmod(helper.string().c_str(), 0700) == 0,
                 "chmod helper executable should succeed");

    auto present = pqnas::check_opaque_backend_status();
    require_true(present.credentials_file_exists, "credentials file should exist");
    require_true(present.credentials_file_readable, "credentials file should be readable");
    require_true(present.credentials_store_valid, "credentials store should parse as valid");
    require_true(present.credentials_account_count == 0, "empty credentials store should report zero accounts");
    require_true(present.server_setup_file_exists, "server setup file should exist");
    require_true(present.server_setup_file_readable, "server setup file should be readable");
    require_true(present.server_setup_valid, "server setup should pass helper validation");
    require_true(present.helper_exists, "helper should exist");
    require_true(present.helper_executable, "helper should be executable");
    require_true(present.helper_version_ok, "helper --version preflight should pass");
    require_true(present.helper_self_test_ok, "helper self-test preflight should pass");
    require_true(!contains_reason(present.missing_or_not_ready, "opaque_helper_version_failed"),
                 "successful helper should not report version failure");
    require_true(!contains_reason(present.missing_or_not_ready, "opaque_helper_self_test_failed"),
                 "successful helper should not report self-test failure");

    require_true(!present.ready_for_login,
                 "backend must still fail closed until real OPAQUE login is implemented");
    require_true(contains_reason(present.missing_or_not_ready, "opaque_real_login_not_implemented"),
                 "real-login-not-implemented reason should be reported internally");
    require_true(pqnas::opaque_backend_public_error(present) == "opaque_backend_not_configured",
                 "public error should remain generic even when files exist");
    const std::string present_diag = pqnas::opaque_backend_internal_diagnostic_json(present);
    require_true(contains_text(present_diag, "\"credentials_store_valid\":true"),
                 "internal diagnostic should report credentials store parse success");
    require_true(contains_text(present_diag, "\"credentials_account_count\":0"),
                 "internal diagnostic should report empty credentials account count");
    require_true(contains_text(present_diag, "\"helper_version_ok\":true"),
                 "internal diagnostic should report helper version success");
    require_true(contains_text(present_diag, "\"helper_self_test_ok\":true"),
                 "internal diagnostic should report helper self-test success");
    require_true(contains_text(present_diag, "\"server_setup_valid\":true"),
                 "internal diagnostic should report server setup validation success");
    require_true(contains_text(present_diag, "opaque_real_login_not_implemented"),
                 "internal diagnostic should still report real login not implemented");

    write_file(helper,
               "#!/bin/sh\n"
               "if [ \"$1\" = \"--version\" ]; then\n"
               "  echo \"pqnas_opaque_helper test-scaffold\"\n"
               "  exit 0\n"
               "fi\n"
               "if [ \"$1\" = \"self-test\" ]; then\n"
               "  echo \"self-test deliberately failed\"\n"
               "  exit 42\n"
               "fi\n"
               "if [ \"$1\" = \"server-setup-check\" ]; then\n"
               "  echo '{\"ok\":true,\"op\":\"server-setup-check\",\"bytes_read\":27}'\n"
               "  exit 0\n"
               "fi\n"
               "exit 9\n");
    require_true(::chmod(helper.string().c_str(), 0700) == 0,
                 "chmod failing helper executable should succeed");

    auto broken_helper = pqnas::check_opaque_backend_status();
    require_true(broken_helper.helper_exists, "failing helper should still exist");
    require_true(broken_helper.helper_executable, "failing helper should still be executable");
    require_true(broken_helper.helper_version_ok, "failing helper version preflight should still pass");
    require_true(!broken_helper.helper_self_test_ok, "failing helper self-test preflight should fail");
    require_true(contains_reason(broken_helper.missing_or_not_ready, "opaque_helper_self_test_failed"),
                 "failing helper self-test reason should be reported internally");
    require_true(!broken_helper.ready_for_login,
                 "backend must remain fail-closed when helper self-test fails");
    require_true(pqnas::opaque_backend_public_error(broken_helper) == "opaque_backend_not_configured",
                 "public error should remain generic when helper self-test fails");
    const std::string broken_diag = pqnas::opaque_backend_internal_diagnostic_json(broken_helper);
    require_true(contains_text(broken_diag, "\"helper_self_test_ok\":false"),
                 "internal diagnostic should report helper self-test failure");
    require_true(contains_text(broken_diag, "opaque_helper_self_test_failed"),
                 "internal diagnostic should include helper self-test failure reason");

    fs::remove_all(root, ec);
    unset_opaque_env();

    std::cout << "ok: OPAQUE backend status scaffold tests passed\n";
    return 0;
}
