#include "federation/pqnas_nodus_client.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <sys/wait.h>
#include <unistd.h>
#include <map>
#include <memory>
#include <mutex>

namespace pqnas::federation {
namespace {

std::mutex& nodus_cli_mutex_for_scope(const std::string& scope) {
    static std::mutex map_mutex;
    static std::map<std::string, std::shared_ptr<std::mutex>> mutexes;

    const std::string key = scope.empty() ? "default" : scope;

    std::lock_guard<std::mutex> lock(map_mutex);

    auto it = mutexes.find(key);
    if (it != mutexes.end() && it->second) {
        return *it->second;
    }

    auto created = std::make_shared<std::mutex>();
    auto& ref = *created;
    mutexes[key] = created;
    return ref;
}

NodusCommandResult run_argv_capture(
    const std::vector<std::string>& argv,
    const std::string& serialization_scope
) {
    // Research adapter safety:
    // keep nodus-cli calls serialized per identity+seed, but avoid one global
    // lock that blocks independent seed operations across the whole server.
    std::lock_guard<std::mutex> lock(nodus_cli_mutex_for_scope(serialization_scope));

    NodusCommandResult result;

    if (argv.empty() || argv.front().empty()) {
        result.exit_code = -1;
        result.output = "empty argv";
        return result;
    }

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) {
        result.exit_code = -1;
        result.output = std::string("pipe failed: ") + std::strerror(errno);
        return result;
    }

    pid_t pid = ::fork();
    if (pid < 0) {
        const int e = errno;
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        result.exit_code = -1;
        result.output = std::string("fork failed: ") + std::strerror(e);
        return result;
    }

    if (pid == 0) {
        ::close(pipefd[0]);

        if (::dup2(pipefd[1], STDOUT_FILENO) < 0 ||
            ::dup2(pipefd[1], STDERR_FILENO) < 0) {
            ::_exit(126);
        }

        ::close(pipefd[1]);

        std::vector<char*> c_argv;
        c_argv.reserve(argv.size() + 1);
        for (const auto& arg : argv) {
            c_argv.push_back(const_cast<char*>(arg.c_str()));
        }
        c_argv.push_back(nullptr);

        ::execvp(c_argv[0], c_argv.data());
        ::_exit(127);
    }

    ::close(pipefd[1]);

    constexpr std::size_t kMaxCommandOutputBytes = 64 * 1024;
    bool output_truncated = false;
    std::array<char, 4096> buffer{};

    for (;;) {
        const ssize_t n = ::read(pipefd[0], buffer.data(), buffer.size());
        if (n > 0) {
            const std::size_t chunk_size = static_cast<std::size_t>(n);

            if (result.output.size() < kMaxCommandOutputBytes) {
                const std::size_t remaining =
                    kMaxCommandOutputBytes - result.output.size();
                const std::size_t take = std::min(remaining, chunk_size);
                result.output.append(buffer.data(), take);

                if (chunk_size > take) {
                    output_truncated = true;
                }
            } else {
                output_truncated = true;
            }

            continue;
        }

        if (n == 0) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        result.output += std::string("\nread failed: ") + std::strerror(errno);
        break;
    }

    ::close(pipefd[0]);

    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR) continue;

        result.exit_code = -1;
        if (result.output.empty()) {
            result.output = std::string("waitpid failed: ") + std::strerror(errno);
        }
        return result;
    }

    if (output_truncated) {
        result.output += "\n...[truncated at 65536 bytes]";
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    } else {
        result.exit_code = status;
    }

    return result;
}

std::string nodus_cli_serialization_scope(
    const NodusClientConfig& config,
    const NodusSeed& seed
) {
    std::ostringstream scope;
    scope << (config.identity_dir.empty() ? "<no-identity>" : config.identity_dir)
          << "\n"
          << seed.host
          << ":"
          << seed.client_port;
    return scope.str();
}

std::vector<std::string> build_base_argv(const NodusClientConfig& config, const NodusSeed& seed) {
    if (config.nodus_cli_path.empty()) {
        throw std::invalid_argument("nodus_cli_path is empty");
    }
    if (seed.host.empty()) {
        throw std::invalid_argument("Nodus seed host is empty");
    }
    if (seed.client_port <= 0 || seed.client_port > 65535) {
        throw std::invalid_argument("Nodus seed client port is invalid");
    }

    std::vector<std::string> argv;

    if (config.timeout_seconds > 0) {
        argv.push_back("timeout");
        argv.push_back(std::to_string(config.timeout_seconds));
    }

    argv.push_back(config.nodus_cli_path);
    argv.push_back("-s");
    argv.push_back(seed.host);
    argv.push_back("-p");
    argv.push_back(std::to_string(seed.client_port));

    if (!config.identity_dir.empty()) {
        argv.push_back("-i");
        argv.push_back(config.identity_dir);
    }

    return argv;
}

} // namespace

std::string nodus_trim_copy(const std::string& in) {
    std::size_t a = 0;
    while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a]))) ++a;

    std::size_t b = in.size();
    while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;

    return in.substr(a, b - a);
}

bool parse_nodus_seed_token(
    const std::string& token_raw,
    int ordinal,
    NodusSeed* out
) {
    if (!out) return false;

    const std::string token = nodus_trim_copy(token_raw);
    if (token.empty()) return false;

    std::string name;
    std::string endpoint = token;

    const std::size_t eq = token.find('=');
    if (eq != std::string::npos) {
        name = nodus_trim_copy(token.substr(0, eq));
        endpoint = nodus_trim_copy(token.substr(eq + 1));
    }

    const std::size_t colon = endpoint.rfind(':');
    if (colon == std::string::npos || colon == 0 || colon + 1 >= endpoint.size()) {
        return false;
    }

    const std::string host = nodus_trim_copy(endpoint.substr(0, colon));
    const std::string port_s = nodus_trim_copy(endpoint.substr(colon + 1));

    if (host.empty() || port_s.empty()) return false;

    int port = 0;
    try {
        std::size_t consumed = 0;
        port = std::stoi(port_s, &consumed, 10);
        if (consumed != port_s.size()) return false;
    } catch (...) {
        return false;
    }

    if (port <= 0 || port > 65535) return false;

    if (name.empty()) {
        name = "seed-" + std::to_string(ordinal);
    }

    *out = NodusSeed{name, host, port};
    return true;
}

std::vector<NodusSeed> nodus_seeds_from_env() {
    std::vector<NodusSeed> out;

    const char* raw = std::getenv("PQNAS_NODUS_SEEDS");
    if (!raw) return out;

    std::stringstream ss(raw);
    std::string token;
    int ordinal = 1;

    while (std::getline(ss, token, ',')) {
        NodusSeed seed;
        if (parse_nodus_seed_token(token, ordinal, &seed)) {
            out.push_back(seed);
            ++ordinal;
        }
    }

    return out;
}

std::vector<NodusSeed> default_nodus_seeds() {
    const auto env_seeds = nodus_seeds_from_env();
    if (!env_seeds.empty()) {
        return env_seeds;
    }

    return {
        {"US-1", "154.38.182.161", 4001},

        // Current public research bootstrap/client endpoints.
        // Override with PQNAS_NODUS_SEEDS=name=host:port,name2=host2:port.
        {"EU-4", "164.68.105.227", 4001},
        {"EU-5", "164.68.116.180", 4001},
    };
}


std::string shell_quote_for_nodus_research(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('\'');

    for (char c : value) {
        if (c == '\'') {
            out += "'\\''";
        } else {
            out.push_back(c);
        }
    }

    out.push_back('\'');
    return out;
}

NodusCommandResult nodus_cli_put(
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& key,
    const std::string& value)
{
    if (key.empty()) {
        throw std::invalid_argument("Nodus key is empty");
    }

    auto argv = build_base_argv(config, seed);
    argv.push_back("put");
    argv.push_back(key);
    argv.push_back(value);

    return run_argv_capture(argv, nodus_cli_serialization_scope(config, seed));
}

NodusCommandResult nodus_cli_get(
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& key)
{
    if (key.empty()) {
        throw std::invalid_argument("Nodus key is empty");
    }

    auto argv = build_base_argv(config, seed);
    argv.push_back("get");
    argv.push_back(key);

    return run_argv_capture(argv, nodus_cli_serialization_scope(config, seed));
}

} // namespace pqnas::federation
