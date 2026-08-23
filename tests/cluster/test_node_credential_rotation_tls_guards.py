#!/usr/bin/env python3

from pathlib import Path

service = Path(
    "server/src/cluster/node_credential_rotation_service.cpp"
).read_text()

header = Path(
    "server/src/cluster/node_credential_rotation_service.h"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    header.count("std::string tls_spki_sha256_pin;") == 2,
    "prepare and finalize requests must both carry the TLS pin",
)

require(
    "const NodeTlsTrustStore* tls_trust_store" in header,
    "rotation config must require the TLS trust store",
)

trust_pos = service.find(
    "config_.tls_trust_store->get("
)

bearer_pos = service.find(
    "->make_bearer_authorization("
)

require(
    trust_pos >= 0,
    "rotation must resolve authoritative TLS trust",
)

require(
    bearer_pos >= 0 and
    trust_pos < bearer_pos,
    "TLS trust must be resolved before OLD bearer materialization",
)

require(
    "finalize_request.tls_spki_sha256_pin =" in service and
    "prepare_request.tls_spki_sha256_pin;" in service,
    "finalize must preserve the exact prepare TLS pin",
)

require(
    "bind_if_absent(" not in service and
    "replace_tls" not in service and
    "upsert_tls" not in service,
    "ordinary credential rotation must never mutate TLS trust",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print(
    "OK: node credential rotation TLS structural guards passed"
)
