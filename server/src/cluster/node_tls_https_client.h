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

} // namespace pqnas::cluster
