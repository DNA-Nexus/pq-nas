#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def read(path: str) -> str:
    p = ROOT / path
    if not p.exists():
        die(f"missing file: {path}")
    return p.read_text(encoding="utf-8")

def write_new_or_same(path: str, content: str) -> None:
    p = ROOT / path
    p.parent.mkdir(parents=True, exist_ok=True)
    if p.exists():
        old = p.read_text(encoding="utf-8")
        if old != content:
            die(f"{path} already exists with different content")
        print(f"unchanged: {path}")
        return
    p.write_text(content, encoding="utf-8")
    print(f"created: {path}")

def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    if old not in text:
        die(f"anchor not found in {path}: {old!r}")
    if new in text:
        print(f"unchanged: {path}")
        return
    text = text.replace(old, new, 1)
    (ROOT / path).write_text(text, encoding="utf-8")
    print(f"patched: {path}")

helper_h = r'''#pragma once

#include <filesystem>
#include <string>

namespace pqnas {

struct OpaqueHelperClientResult {
    bool ok = false;
    int exit_code = -1;
    std::string output;
    std::string error;
};

class OpaqueHelperClient {
public:
    explicit OpaqueHelperClient(std::filesystem::path helper_path);

    const std::filesystem::path& helper_path() const;

    OpaqueHelperClientResult version() const;
    OpaqueHelperClientResult self_test() const;

private:
    OpaqueHelperClientResult run_allowed_command(const std::string& arg) const;

    std::filesystem::path helper_path_;
};

} // namespace pqnas
'''

helper_cpp = r'''#include "opaque_helper_client.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pqnas {
namespace {

constexpr std::size_t kMaxHelperOutputBytes = 64 * 1024;
constexpr auto kHelperTimeout = std::chrono::seconds(5);

bool is_allowed_helper_arg(const std::string& arg) {
    return arg == "--version" || arg == "self-test";
}

std::string errno_string(const std::string& prefix) {
    return prefix + ": " + std::strerror(errno);
}

void append_limited(std::string& out, const char* data, std::size_t n) {
    if (out.size() >= kMaxHelperOutputBytes) {
        return;
    }

    const std::size_t room = kMaxHelperOutputBytes - out.size();
    const std::size_t take = std::min(room, n);
    out.append(data, take);

    if (take < n) {
        static constexpr const char* truncated = "\n[opaque helper output truncated]\n";
        const std::size_t len = std::strlen(truncated);
        if (out.size() + len <= kMaxHelperOutputBytes) {
            out.append(truncated, len);
        }
    }
}

bool set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

bool read_available(int fd, std::string& out) {
    char buf[4096];

    while (true) {
        const ssize_t n = ::read(fd, buf, sizeof(buf));
        if (n > 0) {
            append_limited(out, buf, static_cast<std::size_t>(n));
            continue;
        }

        if (n == 0) {
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        append_limited(out, "\n[opaque helper pipe read failed]\n", 34);
        return false;
    }
}

int decode_wait_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }

    return -1;
}

} // namespace

OpaqueHelperClient::OpaqueHelperClient(std::filesystem::path helper_path)
    : helper_path_(std::move(helper_path)) {
}

const std::filesystem::path& OpaqueHelperClient::helper_path() const {
    return helper_path_;
}

OpaqueHelperClientResult OpaqueHelperClient::version() const {
    return run_allowed_command("--version");
}

OpaqueHelperClientResult OpaqueHelperClient::self_test() const {
    return run_allowed_command("self-test");
}

OpaqueHelperClientResult OpaqueHelperClient::run_allowed_command(const std::string& arg) const {
    OpaqueHelperClientResult result;

    if (!is_allowed_helper_arg(arg)) {
        result.error = "opaque_helper_command_not_allowed";
        return result;
    }

    if (helper_path_.empty()) {
        result.error = "opaque_helper_path_empty";
        return result;
    }

    const std::string helper = helper_path_.string();
    if (::access(helper.c_str(), X_OK) != 0) {
        result.error = "opaque_helper_not_executable";
        return result;
    }

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        result.error = errno_string("opaque_helper_pipe_failed");
        return result;
    }

    if (!set_nonblocking(pipefd[0])) {
        result.error = errno_string("opaque_helper_pipe_nonblocking_failed");
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return result;
    }

    const pid_t pid = ::fork();
    if (pid < 0) {
        result.error = errno_string("opaque_helper_fork_failed");
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return result;
    }

    if (pid == 0) {
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[0]);
        ::close(pipefd[1]);

        std::string child_helper = helper;
        std::string child_arg = arg;

        char* const argv[] = {
            const_cast<char*>(child_helper.c_str()),
            const_cast<char*>(child_arg.c_str()),
            nullptr
        };

        ::execv(child_helper.c_str(), argv);

        const std::string msg = errno_string("execv pqnas_opaque_helper failed") + "\n";
        (void)::write(STDERR_FILENO, msg.data(), msg.size());
        _exit(127);
    }

    ::close(pipefd[1]);

    bool pipe_open = true;
    bool child_done = false;
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + kHelperTimeout;

    while (pipe_open || !child_done) {
        if (!child_done) {
            const pid_t waited = ::waitpid(pid, &status, WNOHANG);
            if (waited == pid) {
                child_done = true;
            } else if (waited < 0 && errno != EINTR) {
                result.error = errno_string("opaque_helper_waitpid_failed");
                break;
            }
        }

        if (pipe_open) {
            pollfd pfd{};
            pfd.fd = pipefd[0];
            pfd.events = POLLIN | POLLHUP | POLLERR;

            const int poll_rc = ::poll(&pfd, 1, 50);
            if (poll_rc > 0) {
                pipe_open = read_available(pipefd[0], result.output);
            } else if (poll_rc < 0 && errno != EINTR) {
                result.error = errno_string("opaque_helper_poll_failed");
                break;
            }
        }

        if (!child_done && std::chrono::steady_clock::now() > deadline) {
            (void)::kill(pid, SIGKILL);
            (void)::waitpid(pid, &status, 0);
            child_done = true;
            result.error = "opaque_helper_timeout";
            result.exit_code = -1;
            break;
        }

        if (child_done && !pipe_open) {
            break;
        }
    }

    if (pipe_open) {
        (void)read_available(pipefd[0], result.output);
    }

    ::close(pipefd[0]);

    if (!child_done) {
        while (::waitpid(pid, &status, 0) < 0) {
            if (errno != EINTR) {
                result.error = errno_string("opaque_helper_waitpid_failed");
                break;
            }
        }
    }

    if (result.error.empty()) {
        result.exit_code = decode_wait_status(status);
        result.ok = (result.exit_code == 0);
        if (!result.ok) {
            result.error = "opaque_helper_command_failed";
        }
    }

    return result;
}

} // namespace pqnas
'''

test_cpp = r'''#include "opaque_helper_client.h"

#include <cstdlib>
#include <filesystem>
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

    pqnas::OpaqueHelperClient missing(std::filesystem::temp_directory_path() /
                                      "pqnas_missing_opaque_helper_for_client_test");
    const auto missing_result = missing.version();
    require_true(!missing_result.ok, "missing helper must fail closed");
    require_true(missing_result.error == "opaque_helper_not_executable",
                 "missing helper should report not executable");

    std::cout << "ok: OPAQUE helper client scaffold tests passed\n";
    return 0;
}
'''

write_new_or_same("server/src/opaque_helper_client.h", helper_h)
write_new_or_same("server/src/opaque_helper_client.cpp", helper_cpp)
write_new_or_same("tests/opaque_helper_client/test_opaque_helper_client.cpp", test_cpp)

replace_once(
    "CMakeLists.txt",
    "        server/src/opaque_backend_status.cpp\n",
    "        server/src/opaque_backend_status.cpp\n"
    "        server/src/opaque_helper_client.cpp\n",
)

cmake_anchor = """# -----------------------------------------------------------------------------
# Test: test_opaque_backend_status
# -----------------------------------------------------------------------------
"""
cmake_insert = """# -----------------------------------------------------------------------------
# Test: test_opaque_helper_client
# -----------------------------------------------------------------------------
add_executable(test_opaque_helper_client
        tests/opaque_helper_client/test_opaque_helper_client.cpp
        server/src/opaque_helper_client.cpp
)

target_include_directories(test_opaque_helper_client PRIVATE
        ${CMAKE_SOURCE_DIR}/server/src
)

set_target_properties(test_opaque_helper_client PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_custom_target(run_test_opaque_helper_client
        COMMAND "${CMAKE_BINARY_DIR}/bin/test_opaque_helper_client" "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS test_opaque_helper_client pqnas_opaque_helper
)


# -----------------------------------------------------------------------------
# Test: test_opaque_backend_status
# -----------------------------------------------------------------------------
"""
replace_once("CMakeLists.txt", cmake_anchor, cmake_insert)

replace_once(
    "docs/technical/opaque_login_design.md",
    "- `OpaqueBackendStatus` exists as a fail-closed backend readiness/preflight scaffold.\n",
    "- `OpaqueBackendStatus` exists as a fail-closed backend readiness/preflight scaffold.\n"
    "- `OpaqueHelperClient` exists as a C++ helper-client scaffold that can call only `pqnas_opaque_helper --version` and `pqnas_opaque_helper self-test`.\n",
)

print("done")
