#include "opaque_helper_client.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

[[noreturn]] void fail(const std::string& msg) {
    std::cerr << "FAIL: " << msg << "\n";
    std::exit(1);
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        fail("usage: test_opaque_helper_client_rust_roundtrip <rust-helper> <mode> ...");
    }

    const std::filesystem::path helper_path = argv[1];
    const std::string mode = argv[2];

    pqnas::OpaqueHelperClient client(helper_path);

    if (mode == "register-start") {
        if (argc != 6) {
            fail("usage: <rust-helper> register-start <setup-path> <credential-id> <registration-request-b64>");
        }

        const auto result = client.register_start(argv[3], argv[4], argv[5]);
        if (!result.ok) {
            fail("register-start failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\"ok\":true") ||
            !contains(result.output, "\"registration_response_b64\"")) {
            fail("register-start output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    if (mode == "register-finish") {
        if (argc != 4) {
            fail("usage: <rust-helper> register-finish <registration-upload-b64>");
        }

        const auto result = client.register_finish(argv[3]);
        if (!result.ok) {
            fail("register-finish failed: error=" + result.error + " output=" + result.output);
        }

        if (!contains(result.output, "\"ok\":true") ||
            !contains(result.output, "\"opaque_password_file_b64\"")) {
            fail("register-finish output missing expected JSON fields: " + result.output);
        }

        std::cout << result.output;
        return 0;
    }

    fail("unknown mode: " + mode);
}
