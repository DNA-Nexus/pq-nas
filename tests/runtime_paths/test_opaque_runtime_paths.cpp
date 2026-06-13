#include "runtime_paths.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <unistd.h>

namespace {

void fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

void require_true(bool ok, const std::string& msg) {
    if (!ok) fail(msg);
}

std::filesystem::path temp_root() {
    return std::filesystem::temp_directory_path() /
           ("pqnas_opaque_runtime_paths_" + std::to_string(static_cast<long long>(::getpid())));
}

void unset_opaque_env() {
    ::unsetenv("PQNAS_CONFIG_ROOT");
    ::unsetenv("PQNAS_CONFIG");
    ::unsetenv("PQNAS_OPAQUE_CREDENTIALS_PATH");
    ::unsetenv("PQNAS_OPAQUE_SERVER_SETUP_PATH");
    ::unsetenv("PQNAS_OPAQUE_HELPER");
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    unset_opaque_env();

    require_true(pqnas::config_root_path() == fs::path("/etc/pqnas"),
                 "default config root should be /etc/pqnas");
    require_true(pqnas::opaque_credentials_path() == fs::path("/etc/pqnas/opaque_credentials.json"),
                 "default OPAQUE credentials path should be under /etc/pqnas");
    require_true(pqnas::opaque_server_setup_path() == fs::path("/etc/pqnas/opaque_server_setup.bin"),
                 "default OPAQUE server setup path should be under /etc/pqnas");
    require_true(pqnas::opaque_helper_path() == fs::path("/usr/local/libexec/pqnas/pqnas_opaque_helper"),
                 "default OPAQUE helper path should be libexec path");

    const fs::path root = temp_root();
    const fs::path legacy_root = root / "legacy_pqnas_config";
    require_true(::setenv("PQNAS_CONFIG", legacy_root.string().c_str(), 1) == 0,
                 "setenv PQNAS_CONFIG should succeed");

    require_true(pqnas::config_root_path() == legacy_root,
                 "PQNAS_CONFIG should override config root when PQNAS_CONFIG_ROOT is unset");
    require_true(pqnas::opaque_credentials_path() == legacy_root / "opaque_credentials.json",
                 "PQNAS_CONFIG fallback should affect credentials path");
    require_true(pqnas::opaque_server_setup_path() == legacy_root / "opaque_server_setup.bin",
                 "PQNAS_CONFIG fallback should affect server setup path");

    require_true(::setenv("PQNAS_CONFIG_ROOT", root.string().c_str(), 1) == 0,
                 "setenv PQNAS_CONFIG_ROOT should succeed");

    require_true(pqnas::config_root_path() == root,
                 "PQNAS_CONFIG_ROOT should override PQNAS_CONFIG");
    require_true(pqnas::opaque_credentials_path() == root / "opaque_credentials.json",
                 "config root override should affect credentials path");
    require_true(pqnas::opaque_server_setup_path() == root / "opaque_server_setup.bin",
                 "config root override should affect server setup path");

    require_true(::setenv("PQNAS_OPAQUE_CREDENTIALS_PATH", "/tmp/custom_opaque_credentials.json", 1) == 0,
                 "setenv PQNAS_OPAQUE_CREDENTIALS_PATH should succeed");
    require_true(::setenv("PQNAS_OPAQUE_SERVER_SETUP_PATH", "/tmp/custom_opaque_server_setup.bin", 1) == 0,
                 "setenv PQNAS_OPAQUE_SERVER_SETUP_PATH should succeed");
    require_true(::setenv("PQNAS_OPAQUE_HELPER", "/tmp/custom_pqnas_opaque_helper", 1) == 0,
                 "setenv PQNAS_OPAQUE_HELPER should succeed");

    require_true(pqnas::opaque_credentials_path() == fs::path("/tmp/custom_opaque_credentials.json"),
                 "explicit credentials path override should win");
    require_true(pqnas::opaque_server_setup_path() == fs::path("/tmp/custom_opaque_server_setup.bin"),
                 "explicit server setup path override should win");
    require_true(pqnas::opaque_helper_path() == fs::path("/tmp/custom_pqnas_opaque_helper"),
                 "explicit helper path override should win");

    unset_opaque_env();

    std::cout << "ok: OPAQUE runtime path tests passed\n";
    return 0;
}
