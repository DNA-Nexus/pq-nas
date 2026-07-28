#pragma once

#include <string>

namespace pqnas::cluster {

enum class ServerRole {
    Standalone,
    Director,
    Node,
};

struct ServerRoleConfig {
    ServerRole role = ServerRole::Standalone;
    std::string node_id;
    std::string director_url;
    std::string cluster_credential_file;

    bool is_standalone() const { return role == ServerRole::Standalone; }
    bool is_director() const { return role == ServerRole::Director; }
    bool is_node() const { return role == ServerRole::Node; }
};

const char* server_role_name(ServerRole role);

// Parse explicit values separately from getenv() so validation can be tested
// without mutating process-wide environment state.
bool parse_server_role_config(
    const std::string& raw_role,
    const std::string& raw_node_id,
    const std::string& raw_director_url,
    const std::string& raw_cluster_credential_file,
    ServerRoleConfig* out,
    std::string* err
);

bool load_server_role_config_from_env(
    ServerRoleConfig* out,
    std::string* err
);

} // namespace pqnas::cluster
