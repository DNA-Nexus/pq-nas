#include "opaque_helper_client.h"

#include <cstdlib>
#include <filesystem>
#include <unistd.h>
#include <fstream>
#include <iostream>
#include <string>

namespace {

void fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

void require_true(bool ok, const std::string& msg) {
    if (!ok) fail(msg);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        fail("usage: test_opaque_helper_client <path-to-pqnas_opaque_helper>");
    }

    const std::filesystem::path helper_path = argv[1];
    pqnas::OpaqueHelperClient client(helper_path);

    require_true(client.helper_path() == helper_path, "helper path should be stored exactly");

    const auto version = client.version();
    require_true(version.ok, "helper --version should succeed: " + version.error + " output=" + version.output);
    require_true(version.exit_code == 0, "helper --version should exit 0");
    require_true(contains(version.output, "pqnas_opaque_helper"),
                 "helper --version output should identify pqnas_opaque_helper");

    const auto self_test = client.self_test();
    require_true(self_test.ok, "helper self-test should succeed: " + self_test.error + " output=" + self_test.output);
    require_true(self_test.exit_code == 0, "helper self-test should exit 0");
    require_true(contains(self_test.output, "scaffold self-test passed"),
                 "helper self-test output should remain scaffold-only");

    const auto empty_register_start =
        client.register_start(std::filesystem::temp_directory_path() / "opaque_server_setup.bin", "", "QUJD");
    require_true(!empty_register_start.ok, "empty register-start credential id must fail closed");
    require_true(empty_register_start.error == "opaque_helper_command_not_allowed",
                 "empty register-start credential id should be rejected before exec");

    const auto empty_register_finish = client.register_finish("");
    require_true(!empty_register_finish.ok, "empty register-finish upload must fail closed");
    require_true(empty_register_finish.error == "opaque_helper_command_not_allowed",
                 "empty register-finish upload should be rejected before exec");

    const auto malformed_register_finish = client.register_finish("QUJD");
    require_true(!malformed_register_finish.ok,
                 "malformed register-finish payload must fail closed");

    pqnas::OpaqueHelperClient missing(std::filesystem::temp_directory_path() /
                                      "pqnas_missing_opaque_helper_for_client_test");
    const auto missing_result = missing.version();
    require_true(!missing_result.ok, "missing helper must fail closed");
    require_true(missing_result.error == "opaque_helper_not_executable",
                 "missing helper should report not executable");

    std::cout << "ok: OPAQUE helper client scaffold tests passed\n";
    return 0;
}
