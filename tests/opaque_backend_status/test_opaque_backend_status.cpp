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

    write_file(credentials, "{ \"version\": 1, \"accounts\": [] }\n");
    write_file(setup, "fake-server-setup-placeholder\n");
    write_file(helper, "#!/bin/sh\nexit 0\n");
    require_true(::chmod(helper.string().c_str(), 0700) == 0,
                 "chmod helper executable should succeed");

    auto present = pqnas::check_opaque_backend_status();
    require_true(present.credentials_file_exists, "credentials file should exist");
    require_true(present.credentials_file_readable, "credentials file should be readable");
    require_true(present.server_setup_file_exists, "server setup file should exist");
    require_true(present.server_setup_file_readable, "server setup file should be readable");
    require_true(present.helper_exists, "helper should exist");
    require_true(present.helper_executable, "helper should be executable");

    require_true(!present.ready_for_login,
                 "backend must still fail closed until real OPAQUE login is implemented");
    require_true(contains_reason(present.missing_or_not_ready, "opaque_real_login_not_implemented"),
                 "real-login-not-implemented reason should be reported internally");
    require_true(pqnas::opaque_backend_public_error(present) == "opaque_backend_not_configured",
                 "public error should remain generic even when files exist");

    fs::remove_all(root, ec);
    unset_opaque_env();

    std::cout << "ok: OPAQUE backend status scaffold tests passed\n";
    return 0;
}
