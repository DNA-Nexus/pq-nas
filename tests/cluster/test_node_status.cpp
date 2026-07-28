#include "cluster/node_status.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    pqnas::cluster::NodeStatusSnapshot status;

    status.node_id = "node-01";
    status.hostname = "cellnode2";
    status.server_version = "1.2.6";
    status.uptime_s = 123456;

    status.storage.total_bytes = 1000;
    status.storage.used_bytes = 400;
    status.storage.free_bytes = 600;
    status.storage.fstype = "ext4";

    status.users.total = 12;
    status.users.enabled = 10;

    status.timestamp = "2026-07-28T05:00:00Z";

    const auto json =
        pqnas::cluster::node_status_to_json(status);

    expect(json["ok"] == true, "ok field");
    expect(
        json["protocol_version"] ==
            pqnas::cluster::kNodeStatusProtocolVersion,
        "protocol version"
    );

    expect(json["node_id"] == "node-01", "node ID");
    expect(json["role"] == "node", "node role");
    expect(json["hostname"] == "cellnode2", "hostname");
    expect(json["server_version"] == "1.2.6", "server version");
    expect(json["uptime_s"] == 123456, "uptime");

    expect(
        json["storage"]["total_bytes"] == 1000,
        "storage total"
    );
    expect(
        json["storage"]["used_bytes"] == 400,
        "storage used"
    );
    expect(
        json["storage"]["free_bytes"] == 600,
        "storage free"
    );
    expect(
        json["storage"]["fstype"] == "ext4",
        "storage filesystem"
    );

    expect(json["users"]["total"] == 12, "user total");
    expect(json["users"]["enabled"] == 10, "enabled users");
    expect(
        json["timestamp"] == "2026-07-28T05:00:00Z",
        "timestamp"
    );

    // Security regression guard: the Director does not need local paths,
    // devices, mount options, or individual user identity information.
    expect(
        !json["storage"].contains("root"),
        "storage root must not be exposed"
    );
    expect(
        !json["storage"].contains("mountpoint"),
        "mountpoint must not be exposed"
    );
    expect(
        !json["storage"].contains("source"),
        "device source must not be exposed"
    );
    expect(
        !json["storage"].contains("options"),
        "mount options must not be exposed"
    );
    expect(
        !json.contains("fingerprint"),
        "user fingerprints must not be exposed"
    );
    expect(
        !json.contains("process"),
        "process details must not be exposed"
    );
    expect(
        !json.contains("network"),
        "network details must not be exposed"
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " node status test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node status JSON tests passed\n";
    return 0;
}
