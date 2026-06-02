#include "federation/pqnas_nodus_client.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
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

NodusCommandResult run_command_capture(
    const std::string& command,
    const std::string& serialization_scope
) {
    // Research adapter safety:
    // keep nodus-cli calls serialized per identity+seed, but avoid one global
    // lock that blocks independent seed operations across the whole server.
    std::lock_guard<std::mutex> lock(nodus_cli_mutex_for_scope(serialization_scope));

    NodusCommandResult result;

    const std::string command_with_stderr = command + " 2>&1";

    FILE* pipe = popen(command_with_stderr.c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        result.output = "popen failed";
        return result;
    }

    constexpr std::size_t kMaxCommandOutputBytes = 64 * 1024;

    bool output_truncated = false;
    std::array<char, 4096> buffer{};

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        const std::string chunk(buffer.data());

        if (result.output.size() < kMaxCommandOutputBytes) {
            const std::size_t remaining =
                kMaxCommandOutputBytes - result.output.size();
            result.output.append(chunk.data(), std::min(remaining, chunk.size()));

            if (chunk.size() > remaining) {
                output_truncated = true;
            }
        } else {
            output_truncated = true;
        }
    }

    if (output_truncated) {
        result.output += "\n...[truncated at 65536 bytes]";
    }

    const int status = pclose(pipe);

    if (status == -1) {
        result.exit_code = -1;
        if (result.output.empty()) {
            result.output = "pclose failed";
        }
    } else if (WIFEXITED(status)) {
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

std::string build_base_command(const NodusClientConfig& config, const NodusSeed& seed) {
    if (config.nodus_cli_path.empty()) {
        throw std::invalid_argument("nodus_cli_path is empty");
    }
    if (seed.host.empty()) {
        throw std::invalid_argument("Nodus seed host is empty");
    }
    if (seed.client_port <= 0 || seed.client_port > 65535) {
        throw std::invalid_argument("Nodus seed client port is invalid");
    }

    std::ostringstream cmd;
    if (config.timeout_seconds > 0) {
        cmd << "timeout " << config.timeout_seconds << " ";
    }

    cmd << shell_quote_for_nodus_research(config.nodus_cli_path)
        << " -s " << shell_quote_for_nodus_research(seed.host)
        << " -p " << seed.client_port;

    if (!config.identity_dir.empty()) {
        cmd << " -i " << shell_quote_for_nodus_research(config.identity_dir);
    }

    return cmd.str();
}

} // namespace

std::vector<NodusSeed> default_nodus_seeds() {
    return {
        {"US-1", "154.38.182.161", 4001},
        {"EU-1", "161.97.85.25", 4001},
        {"EU-2", "156.67.24.125", 4001},
        {"EU-3", "156.67.25.251", 4001},
        {"EU-4", "164.68.105.227", 4001},
        {"EU-5", "164.68.116.180", 4001},
        {"EU-6", "75.119.141.51", 4001},
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

    std::ostringstream cmd;
    cmd << build_base_command(config, seed)
        << " put "
        << shell_quote_for_nodus_research(key)
        << " "
        << shell_quote_for_nodus_research(value);

    return run_command_capture(cmd.str(), nodus_cli_serialization_scope(config, seed));
}

NodusCommandResult nodus_cli_get(
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& key)
{
    if (key.empty()) {
        throw std::invalid_argument("Nodus key is empty");
    }

    std::ostringstream cmd;
    cmd << build_base_command(config, seed)
        << " get "
        << shell_quote_for_nodus_research(key);

    return run_command_capture(cmd.str(), nodus_cli_serialization_scope(config, seed));
}

} // namespace pqnas::federation
