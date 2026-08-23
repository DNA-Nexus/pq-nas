#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/cluster/node_credential_rotation_https.cpp"
).read_text()

header = Path(
    "server/src/cluster/node_credential_rotation_https.h"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    '"/internal/node/credential/rotation/prepare"' in source and
    '"/internal/node/credential/rotation/finalize"' in source,
    "fixed Storage credential rotation endpoints are present",
)

require(
    source.count("client->Post(") == 1,
    "both transports share one hardened POST implementation",
)

require(
    'client->set_follow_location(false);' in source,
    "redirects remain disabled",
)

require(
    'client->set_keep_alive(false);' in source,
    "keep-alive remains disabled",
)

require(
    'client->set_decompress(false);' in source,
    "automatic decompression remains disabled",
)

require(
    "make_pinned_node_https_client(" in source,
    "rotation HTTPS must use the shared pinned TLS client",
)

require(
    "httplib::SSLClient client(" not in source,
    "rotation HTTPS must not construct a raw SSLClient",
)

require(
    "tls_spki_sha256_pin" in source,
    "rotation HTTPS must propagate the exact per-node TLS pin",
)

require(
    "set_connection_timeout" in source and
    "set_read_timeout" in source and
    "set_write_timeout" in source,
    "network timeouts remain explicitly bounded",
)

require(
    "kNodeCredentialRotationHttpsMaxResponseBytes -" in source,
    "response is bounded while streaming",
)

require(
    "HeaderCredentialWiper" in source and
    "SensitiveStringWiper" in source and
    "JsonCredentialWiper" in source,
    "temporary credential-bearing buffers have explicit wipe guards",
)

require(
    "std::cerr" not in source and
    "std::cout" not in source,
    "HTTPS transport does not log credential-bearing state",
)

require(
    "set_follow_location(true)" not in source and
    "enable_server_certificate_verification(false)" not in source and
    "enable_server_hostname_verification(false)" not in source,
    "no insecure TLS or redirect mode exists",
)

require(
    "http://" not in source,
    "transport contains no plaintext HTTP fallback",
)

require(
    "kNodeCredentialRotationHttpsMaxResponseBytes =\n"
    "        1024u;" in header,
    "credential rotation response cap remains explicit",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print(
    "OK: node credential rotation HTTPS hardening guards passed"
)
