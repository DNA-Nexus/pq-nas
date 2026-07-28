#include "cluster/server_role.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void fail(const std::string& message) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expect_ok(
    const std::string& role,
    const std::string& node_id,
    const std::string& director_url,
    pqnas::cluster::ServerRole expected_role,
    const std::string& expected_node_id = "",
    const std::string& expected_director_url = ""
) {
    pqnas::cluster::ServerRoleConfig cfg;
    std::string err;

    if (!pqnas::cluster::parse_server_role_config(
            role, node_id, director_url, &cfg, &err)) {
        fail("expected valid config for role '" + role + "': " + err);
        return;
    }

    if (cfg.role != expected_role) {
        fail("unexpected parsed role for '" + role + "'");
    }

    if (cfg.node_id != expected_node_id) {
        fail("unexpected node ID for role '" + role + "'");
    }

    if (cfg.director_url != expected_director_url) {
        fail("unexpected Director URL for role '" + role + "'");
    }
}

void expect_error(
    const std::string& role,
    const std::string& node_id,
    const std::string& director_url
) {
    pqnas::cluster::ServerRoleConfig cfg;
    std::string err;

    if (pqnas::cluster::parse_server_role_config(
            role, node_id, director_url, &cfg, &err)) {
        fail("expected invalid config for role '" + role + "'");
        return;
    }

    if (err.empty()) {
        fail("invalid config returned no error for role '" + role + "'");
    }
}

} // namespace

int main() {
    using pqnas::cluster::ServerRole;

    expect_ok("", "", "", ServerRole::Standalone);
    expect_ok("standalone", "", "", ServerRole::Standalone);
    expect_ok(" DIRECTOR ", "", "", ServerRole::Director);

    expect_ok(
        "node",
        "node-01",
        "https://director.local/",
        ServerRole::Node,
        "node-01",
        "https://director.local"
    );

    expect_ok(
        "NODE",
        "dell2.example",
        "https://192.168.1.100:8443",
        ServerRole::Node,
        "dell2.example",
        "https://192.168.1.100:8443"
    );

    expect_error("unknown", "", "");
    expect_error("standalone", "node-01", "");
    expect_error("director", "node-01", "");
    expect_error("director", "", "https://director.local");
    expect_error("node", "", "https://director.local");
    expect_error("node", "../node-01", "https://director.local");
    expect_error("node", "-node-01", "https://director.local");
    expect_error("node", "node-01", "");
    expect_error("node", "node-01", "http://director.local");
    expect_error("node", "node-01", "https://user@director.local");
    expect_error("node", "node-01", "https://director.local/api");

    if (failures != 0) {
        std::cerr << failures << " server role test(s) failed\n";
        return 1;
    }

    std::cout << "OK: server role configuration tests passed\n";
    return 0;
}
