from pathlib import Path

root = Path(__file__).resolve().parents[2]

client = (
    root /
    "server/src/cluster/storage_handoff_ticket_client.cpp"
).read_text(encoding="utf-8")

https = (
    root /
    "server/src/cluster/storage_handoff_ticket_https.cpp"
).read_text(encoding="utf-8")

strict_json = (
    root /
    "server/src/cluster/strict_json_parse.h"
).read_text(encoding="utf-8")

internal_route = (
    root /
    "server/src/routes/routes_internal_storage_handoff.cpp"
).read_text(encoding="utf-8")

main_cpp = (
    root /
    "server/src/main.cpp"
).read_text(encoding="utf-8")

cmake = (
    root /
    "CMakeLists.txt"
).read_text(encoding="utf-8")

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


# Security: TLS trust must be resolved before reusable bearer material exists.
trust_pos = client.find(
    "config_.tls_trust_store->get("
)

bearer_pos = client.find(
    "make_bearer_authorization("
)

transport_pos = client.find(
    "config_.transport(request)"
)

require(
    trust_pos >= 0 and
    bearer_pos > trust_pos and
    transport_pos > bearer_pos,
    "handoff client must resolve TLS trust before bearer before transport",
)

require(
    "SensitiveStringWiper" in client and
    "request.authorization" in client,
    "handoff client must wipe short-lived Authorization material",
)

require(
    "make_pinned_node_https_client(" in https,
    "handoff HTTPS transport must use the shared pinned client",
)

require(
    "httplib::SSLClient(" not in https,
    "handoff HTTPS transport must not construct a raw SSLClient",
)

require(
    "set_follow_location(false)" in https,
    "handoff HTTPS transport must disable redirects",
)

require(
    "set_keep_alive(false)" in https,
    "handoff HTTPS transport must disable keep-alive",
)

require(
    "set_decompress(false)" in https,
    "handoff HTTPS transport must disable decompression",
)

require(
    '"/internal/storage/handoff/ticket"' in https,
    "handoff transport must use the exact internal ticket endpoint",
)

require(
    '"Authorization"' in https and
    "HeaderCredentialWiper" in https,
    "handoff transport must attach and wipe Authorization header material",
)

request_start = https.find(
    "json request_json{"
)

request_end = https.find(
    "};",
    request_start,
)

request_block = (
    https[request_start:request_end]
    if request_start >= 0 and request_end > request_start
    else ""
)

require(
    '"fingerprint"' in request_block and
    '"browser_binding_hash"' in request_block,
    "handoff request must contain fingerprint and browser binding hash",
)

require(
    '"home_node"' not in request_block and
    '"projection_revision"' not in request_block and
    '"revision"' not in request_block,
    "Director must not prescribe Storage placement revision in handoff request",
)

require(
    "kStorageHandoffTicketMaxResponseBytes" in https,
    "handoff HTTPS response must remain size-bounded",
)

first_node_read = client.find(
    "config_.node_registry->get("
)

second_node_read = client.find(
    "config_.node_registry->get(",
    first_node_read + 1,
)

first_trust_read = client.find(
    "config_.tls_trust_store->get("
)

second_trust_read = client.find(
    "config_.tls_trust_store->get(",
    first_trust_read + 1,
)

require(
    first_node_read >= 0 and
    first_trust_read > first_node_read and
    second_node_read > first_trust_read and
    second_trust_read > second_node_read and
    bearer_pos > second_trust_read and
    transport_pos > bearer_pos,
    "handoff client must re-read Node and TLS authority before bearer/network side effect",
)

require(
    "parse_json_reject_duplicate_keys(" in https and
    "parse_json_reject_duplicate_keys(" in strict_json and
    "duplicate_key" in strict_json,
    "handoff HTTPS parser must use the shared duplicate-key rejecting parser",
)

require(
    "parse_json_reject_duplicate_keys(" in internal_route and
    "body = json::parse(" not in internal_route,
    "internal handoff route must reject duplicate JSON fields before selection",
)

require(
    "TicketStringWiper" in https and
    "parsed_ticket_wiper" in https,
    "handoff HTTPS parser must wipe its plaintext ticket DOM copy",
)

require(
    "valid_storage_handoff_ticket_text(" in client and
    "valid_storage_handoff_ticket_text(" in https,
    "client and HTTPS parser must use the shared canonical ticket validator",
)

require(
    "bool valid_ticket_text(" not in client and
    "bool valid_ticket_text(" not in https,
    "handoff protocol layers must not duplicate ticket validation rules",
)


# Production composition guard: Director must use the hardened handoff client
# with authoritative Node, credential and pinned-TLS state.
require(
    '#include "cluster/storage_handoff_ticket_client.h"' in main_cpp and
    '#include "cluster/storage_handoff_ticket_https.h"' in main_cpp,
    "pqnas_server must include handoff client and HTTPS transport",
)

tls_load_pos = main_cpp.find(
    "if (!director_node_tls_trust_store.load("
)

composition_pos = main_cpp.find(
    "StorageHandoffTicketClientConfig"
)

require(
    tls_load_pos >= 0 and
    composition_pos > tls_load_pos,
    "Director handoff client must be composed only after TLS trust loads",
)

require(
    "storage_handoff_ticket_client_config.node_registry =" in main_cpp and
    "&director_node_registry;" in main_cpp and
    "storage_handoff_ticket_client_config.credential_registry =" in main_cpp and
    "&director_node_credential_registry;" in main_cpp and
    "storage_handoff_ticket_client_config.tls_trust_store =" in main_cpp and
    "&director_node_tls_trust_store;" in main_cpp,
    "Director handoff client must use authoritative Director stores",
)

require(
    "make_https_storage_handoff_ticket_transport()" in main_cpp,
    "Director handoff client must use hardened HTTPS transport",
)

require(
    """target_link_libraries(pqnas_server PRIVATE
        pqnas_storage_handoff_ticket_https
)""" in cmake,
    "pqnas_server must link production handoff HTTPS client",
)


if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: Storage handoff ticket client/HTTPS guards passed")
