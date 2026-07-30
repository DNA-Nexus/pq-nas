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

    {
        pqnas::cluster::NodeStatusSnapshot parsed;
        std::string parse_error;

        expect(
            pqnas::cluster::parse_node_status_json(
                json,
                &parsed,
                &parse_error
            ),
            "serialized status should parse: " +
                parse_error
        );

        expect(
            parsed.node_id == status.node_id &&
            parsed.hostname == status.hostname &&
            parsed.server_version ==
                status.server_version &&
            parsed.uptime_s == status.uptime_s &&
            parsed.storage.total_bytes ==
                status.storage.total_bytes &&
            parsed.users.enabled ==
                status.users.enabled &&
            parsed.timestamp == status.timestamp,
            "parsed status should match serialized status"
        );

        auto wrong_role = json;
        wrong_role["role"] = "director";

        expect(
            !pqnas::cluster::parse_node_status_json(
                wrong_role,
                &parsed,
                &parse_error
            ),
            "wrong node role must be rejected"
        );

        auto wrong_protocol = json;
        wrong_protocol["protocol_version"] =
            pqnas::cluster::
                kNodeStatusProtocolVersion + 1;

        expect(
            !pqnas::cluster::parse_node_status_json(
                wrong_protocol,
                &parsed,
                &parse_error
            ),
            "wrong protocol version must be rejected"
        );

        auto unknown_field = json;
        unknown_field["unexpected"] = true;

        expect(
            !pqnas::cluster::parse_node_status_json(
                unknown_field,
                &parsed,
                &parse_error
            ),
            "unknown root field must be rejected"
        );

        auto invalid_users = json;
        invalid_users["users"]["enabled"] =
            invalid_users["users"]["total"]
                .get<std::uint64_t>() + 1;

        expect(
            !pqnas::cluster::parse_node_status_json(
                invalid_users,
                &parsed,
                &parse_error
            ),
            "enabled user count above total must be rejected"
        );

        auto invalid_node_id = json;
        invalid_node_id["node_id"] = "../node";

        expect(
            !pqnas::cluster::parse_node_status_json(
                invalid_node_id,
                &parsed,
                &parse_error
            ),
            "unsafe node ID must be rejected"
        );
    }

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
