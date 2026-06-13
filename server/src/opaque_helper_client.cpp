#include "opaque_helper_client.h"

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
        const ssize_t write_rc = ::write(STDERR_FILENO, msg.data(), msg.size());
        (void)write_rc;
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
