#include <iostream>
#include <string>

namespace {

constexpr const char* kProgramName = "pqnas_opaque_helper";
constexpr const char* kVersion = "0.1.0-scaffold";

bool is_future_opaque_op(const std::string& op) {
    return op == "server-setup-create" ||
           op == "register-start" ||
           op == "register-finish" ||
           op == "login-start" ||
           op == "login-finish";
}

int print_version() {
    std::cout << kProgramName << " " << kVersion << "\n";
    return 0;
}

int run_self_test() {
    // This is intentionally only a scaffold smoke test.
    //
    // Security boundary:
    // - no OPAQUE cryptography is implemented here yet
    // - no password material is accepted
    // - no users.json access is performed
    // - no PQ-NAS session can be minted by this helper
    std::cout << "ok: " << kProgramName << " scaffold self-test passed\n";
    return 0;
}

int fail_closed_not_implemented(const std::string& op) {
    std::cout
        << "{"
        << "\"ok\":false,"
        << "\"op\":\"" << op << "\","
        << "\"error\":\"opaque_backend_not_implemented\","
        << "\"message\":\"OPAQUE helper scaffold only; no production OPAQUE crypto is available yet\""
        << "}\n";
    return 1;
}

int print_usage() {
    std::cerr
        << "Usage:\n"
        << "  " << kProgramName << " --version\n"
        << "  " << kProgramName << " self-test\n"
        << "\n"
        << "Future protocol operations are recognized but fail closed:\n"
        << "  server-setup-create\n"
        << "  register-start\n"
        << "  register-finish\n"
        << "  login-start\n"
        << "  login-finish\n";
    return 2;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        return print_usage();
    }

    const std::string arg = argv[1];

    if (arg == "--version" || arg == "version") {
        return print_version();
    }

    if (arg == "self-test") {
        return run_self_test();
    }

    if (is_future_opaque_op(arg)) {
        return fail_closed_not_implemented(arg);
    }

    std::cerr << "unknown command: " << arg << "\n";
    return print_usage();
}
