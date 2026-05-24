#include "federation/pqnas_nodus_client.h"

#include <array>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace pqnas::federation {
namespace {

NodusCommandResult run_command_capture(const std::string& command) {
    NodusCommandResult result;

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe) {
        result.exit_code = -1;
        result.output = "popen failed";
        return result;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result.output.append(buffer.data());
    }

    result.exit_code = pclose(pipe);
    return result;
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

    return run_command_capture(cmd.str());
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

    return run_command_capture(cmd.str());
}

} // namespace pqnas::federation
