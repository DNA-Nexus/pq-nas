#pragma once

#include "httplib.h"

#include <memory>
#include <string>

namespace pqnas::cluster {

// Constructs a node HTTPS client only when the expected TLS identity is a
// canonical SPKI SHA-256 pin.
//
// Security:
// - missing, malformed or non-canonical pins fail before network I/O
// - exact SPKI match returns NoDecisionMade so normal CA verification continues
// - mismatch/missing peer certificate returns CertificateRejected
// - CertificateAccepted is deliberately never returned
// - certificate-chain and hostname verification remain enabled
std::unique_ptr<httplib::SSLClient>
make_pinned_node_https_client(
    const std::string& host,
    int port,
    const std::string& expected_spki_sha256_pin,
    std::string* error
);

// Verifies a candidate Storage HTTPS endpoint without sending bearer material.
//
// Security:
// - normal CA verification remains enabled
// - hostname verification remains enabled
// - the exact canonical SPKI pin must match
// - redirects are disabled
// - no cluster credential is attached to the probe request
bool verify_pinned_node_https_endpoint(
    const std::string& api_base_url,
    const std::string& expected_spki_sha256_pin,
    std::string* error
);

} // namespace pqnas::cluster
