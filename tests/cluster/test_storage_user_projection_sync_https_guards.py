#!/usr/bin/env python3

from pathlib import Path

source = Path(
    "server/src/cluster/storage_user_projection_sync_https.cpp"
).read_text()

client = Path(
    "server/src/cluster/storage_user_projection_sync_client.cpp"
).read_text()

main = Path(
    "server/src/main.cpp"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    '"/internal/storage/user-projection"' in source,
    "HTTPS transport must use the projection sync route",
)

require(
    "client->set_follow_location(false);" in source,
    "redirects must stay disabled",
)

require(
    "client->set_keep_alive(false);" in source,
    "keep-alive must stay disabled",
)

require(
    "client->set_decompress(false);" in source,
    "decompression must stay disabled",
)

require(
    "client->set_connection_timeout(5, 0);" in source,
    "connection timeout must stay bounded",
)

require(
    "client->set_read_timeout(8, 0);" in source,
    "read timeout must stay bounded",
)

require(
    "client->set_write_timeout(5, 0);" in source,
    "write timeout must stay bounded",
)

require(
    "make_pinned_node_https_client(" in source,
    "projection HTTPS transport must use the shared pinned TLS client",
)

require(
    "httplib::SSLClient client(" not in source,
    "projection HTTPS transport must not construct a raw SSLClient",
)

require(
    "request.tls_spki_sha256_pin" in source,
    "projection HTTPS transport must require the per-node TLS pin",
)

require(
    "enable_server_certificate_verification(false)" not in source,
    "insecure certificate mode must not exist",
)

require(
    "enable_server_hostname_verification(false)" not in source,
    "insecure hostname mode must not exist",
)

require(
    "config_.node_registry->get(" in client,
    "sync client must resolve the projection home node from NodeRegistry",
)

require(
    "make_bearer_authorization(" in client,
    "sync client must obtain a per-node Bearer credential",
)


require(
    "config_.tls_trust_store->get(" in client,
    "sync client must resolve durable TLS trust",
)

require(
    client.find("config_.tls_trust_store->get(") <
    client.find("make_bearer_authorization("),
    "TLS trust lookup must occur before Bearer materialization",
)

# Keep production behavior unchanged until client and synchronization policy
# are separately wired and tested.


# Production phase: hardened HTTPS projection sync is now intentionally
# Director-wired. Handoff remains a separate, still-closed security boundary.
require(
    "StorageUserProjectionSyncClient" in main,
    "Director must production-wire the projection sync client",
)

require(
    "make_https_storage_user_projection_sync_transport()" in main,
    "Director projection sync must use the hardened HTTPS transport",
)

require(
    "register_internal_storage_handoff_routes(" not in main,
    "production projection sync must not implicitly enable Storage handoff",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: storage projection sync HTTPS structural guards passed")
