#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pqnas::federation {

struct NodusSeed {
    std::string name;
    std::string host;
    int client_port = 4001;
};

struct NodusClientConfig {
    std::string nodus_cli_path = "/usr/local/bin/nodus-cli";
    std::string identity_dir;
    int timeout_seconds = 8;
};

struct NodusCommandResult {
    int exit_code = -1;
    std::string output;
};

std::vector<NodusSeed> default_nodus_seeds();

std::string shell_quote_for_nodus_research(const std::string& value);

NodusCommandResult nodus_cli_put(
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& key,
    const std::string& value);

NodusCommandResult nodus_cli_get(
    const NodusClientConfig& config,
    const NodusSeed& seed,
    const std::string& key);

} // namespace pqnas::federation
