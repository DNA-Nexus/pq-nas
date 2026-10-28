#include "cluster/cluster_identity_validation.h"

#include <iostream>
#include <string>

namespace {

int failures = 0;

void fail(
    const std::string& message
) {
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        fail(message);
    }
}

void expect_url_ok(
    const std::string& raw,
    const std::string& expected,
    const std::string& message
) {
    std::string normalized = "unchanged";
    std::string error = "stale";

    const bool ok =
        pqnas::cluster::
            normalize_cluster_https_base_url(
                raw,
                &normalized,
                &error
            );

    expect(ok, message + ": should be accepted");

    expect(
        normalized == expected,
        message + ": unexpected canonical value"
    );

    expect(
        error.empty(),
        message + ": success should clear error"
    );
}

void expect_url_error(
    const std::string& raw,
    const std::string& message
) {
    std::string normalized = "unchanged";
    std::string error;

    const bool ok =
        pqnas::cluster::
            normalize_cluster_https_base_url(
                raw,
                &normalized,
                &error
            );

    expect(!ok, message + ": should be rejected");

    // Security: failed validation must not replace a previously trusted
    // canonical authority retained by the caller.
    expect(
        normalized == "unchanged",
        message + ": failure changed output"
    );

    expect(
        !error.empty(),
        message + ": failure should return an error category"
    );
}

void expect_authority_ok(
    const std::string& raw,
    const std::string& expected_host,
    int expected_port,
    const std::string& message
) {
    pqnas::cluster::ClusterHttpsAuthority
        authority;

    authority.host = "unchanged";
    authority.port = 123;

    std::string error = "stale";

    const bool ok =
        pqnas::cluster::
            parse_cluster_https_authority(
                raw,
                &authority,
                &error
            );

    expect(
        ok,
        message + ": should be accepted"
    );

    expect(
        authority.host == expected_host &&
        authority.port == expected_port,
        message + ": unexpected authority"
    );

    expect(
        error.empty(),
        message + ": success should clear error"
    );
}

void expect_authority_error(
    const std::string& raw,
    const std::string& message
) {
    pqnas::cluster::ClusterHttpsAuthority
        authority;

    authority.host = "unchanged";
    authority.port = 123;

    std::string error;

    const bool ok =
        pqnas::cluster::
            parse_cluster_https_authority(
                raw,
                &authority,
                &error
            );

    expect(
        !ok,
        message + ": should be rejected"
    );

    // Security: failure must not partially replace a trusted authority.
    expect(
        authority.host == "unchanged" &&
        authority.port == 123,
        message + ": failure changed output"
    );

    expect(
        !error.empty(),
        message + ": failure should return an error"
    );
}


} // namespace

int main() {
    using pqnas::cluster::valid_cluster_node_id;

    expect(
        valid_cluster_node_id("node-01"),
        "normal node ID"
    );

    expect(
        valid_cluster_node_id("Node_01.example"),
        "allowed node ID punctuation"
    );

    expect(
        valid_cluster_node_id(
            std::string(
                pqnas::cluster::kClusterNodeIdMaxBytes,
                'a'
            )
        ),
        "maximum-size node ID"
    );

    expect(
        !valid_cluster_node_id(""),
        "empty node ID"
    );

    expect(
        !valid_cluster_node_id("-node-01"),
        "leading punctuation"
    );

    expect(
        !valid_cluster_node_id("../node-01"),
        "traversal-shaped node ID"
    );

    expect(
        !valid_cluster_node_id(" node-01"),
        "leading whitespace"
    );

    expect(
        !valid_cluster_node_id("node-01 "),
        "trailing whitespace"
    );

    expect(
        !valid_cluster_node_id("node/01"),
        "slash in node ID"
    );

    expect(
        !valid_cluster_node_id(
            std::string(
                pqnas::cluster::kClusterNodeIdMaxBytes + 1,
                'a'
            )
        ),
        "oversized node ID"
    );

    expect(
        !valid_cluster_node_id(
            std::string("node-") +
            static_cast<char>(0xc3)
        ),
        "non-ASCII node ID"
    );

    expect_url_ok(
        "https://node-01.internal",
        "https://node-01.internal",
        "DNS authority"
    );

    expect_url_ok(
        "https://node-01.internal/",
        "https://node-01.internal",
        "one trailing slash"
    );

    expect_url_ok(
        "https://192.168.1.100:8443",
        "https://192.168.1.100:8443",
        "IPv4 authority with port"
    );

    expect_url_ok(
        "https://[2001:db8::1]:8443",
        "https://[2001:db8::1]:8443",
        "bracketed IPv6 authority"
    );

    expect_url_error(
        "http://node-01.internal",
        "plaintext control-plane URL"
    );

    expect_url_error(
        " https://node-01.internal",
        "leading URL whitespace"
    );

    expect_url_error(
        "https://node-01.internal ",
        "trailing URL whitespace"
    );

    expect_url_error(
        "https://node-01.internal//",
        "multiple trailing slashes"
    );

    expect_url_error(
        "https://user@node-01.internal",
        "userinfo"
    );

    expect_url_error(
        "https://node-01.internal/status",
        "path"
    );

    expect_url_error(
        "https://node-01.internal?health=1",
        "query"
    );

    expect_url_error(
        "https://node-01.internal#status",
        "fragment"
    );

    expect_url_error(
        R"(https://node-01.internal\status)",
        "backslash delimiter"
    );

    expect_url_error(
        "https://node%2finternal",
        "encoded delimiter syntax"
    );

    expect_url_error(
        "https://:8443",
        "missing host before port"
    );

    expect_url_error(
        "https://node-01.internal:",
        "missing port after separator"
    );

    expect_url_error(
        std::string("https://") +
            std::string(
                pqnas::cluster::
                    kClusterHttpsBaseUrlMaxBytes,
                'a'
            ),
        "oversized base URL"
    );

    expect_url_error(
        std::string("https://node-") +
            static_cast<char>(0xc3),
        "non-ASCII authority"
    );

    expect_authority_ok(
        "https://node-01.internal",
        "node-01.internal",
        443,
        "default HTTPS authority port"
    );

    expect_authority_ok(
        "https://node-01.internal:8443/",
        "node-01.internal",
        8443,
        "custom HTTPS authority port"
    );

    expect_authority_ok(
        "https://192.168.1.107:443",
        "192.168.1.107",
        443,
        "IPv4 HTTPS authority"
    );

    expect_authority_ok(
        "https://[2001:db8::1]:8443",
        "2001:db8::1",
        8443,
        "bracketed IPv6 HTTPS authority"
    );

    expect_authority_error(
        "https://node-01.internal:0",
        "zero HTTPS port"
    );

    expect_authority_error(
        "https://node-01.internal:65536",
        "oversized HTTPS port"
    );

    expect_authority_error(
        "https://node-01.internal:abc",
        "non-numeric HTTPS port"
    );

    expect_authority_error(
        "https://2001:db8::1",
        "unbracketed IPv6 authority"
    );

    expect_authority_error(
        "https://[2001:db8::1",
        "unterminated IPv6 authority"
    );

    {
        std::string error;

        expect(
            !pqnas::cluster::
                normalize_cluster_https_base_url(
                    "https://node-01.internal",
                    nullptr,
                    &error
                ),
            "missing URL output must be rejected"
        );

        expect(
            !error.empty(),
            "missing URL output must return an error"
        );
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " cluster identity validation test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: cluster identity validation tests passed\n";

    return 0;
}
