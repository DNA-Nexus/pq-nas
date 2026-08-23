#!/usr/bin/env python3

from pathlib import Path

main = Path("server/src/main.cpp").read_text()
route = Path(
    "server/src/routes/routes_internal_node_credential_rotation.cpp"
).read_text()
header = Path(
    "server/src/routes/routes_internal_node_credential_rotation.h"
).read_text()
runtime = Path(
    "server/src/cluster/storage_node_credential_runtime.cpp"
).read_text()
cmake = Path("CMakeLists.txt").read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    '#include "routes/routes_internal_node_credential_rotation.h"' in main,
    "main includes internal node credential rotation route",
)

require(
    main.count(
        "register_internal_node_credential_rotation_routes("
    ) == 1,
    "credential rotation route is registered exactly once",
)

require(
    "if (cluster_config.is_node()) {\n"
    "        pqnas::cluster::\n"
    "            InternalNodeCredentialRotationRoutesContext" in main,
    "credential rotation route registration is Node-only",
)

require(
    "internal_node_credential_rotation_context.runtime =\n"
    "            &cluster_credential_runtime;" in main,
    "credential rotation route uses process-lifetime credential runtime",
)

load_pos = main.find(
    "if (!cluster_credential_runtime.load("
)

register_pos = main.find(
    "register_internal_node_credential_rotation_routes("
)

require(
    load_pos >= 0 and
    register_pos >= 0 and
    load_pos < register_pos,
    "credential runtime loads before rotation route registration",
)

require(
    route.count(
        '"/internal/node/credential/rotation/prepare"'
    ) == 1 and
    route.count(
        '"/internal/node/credential/rotation/finalize"'
    ) == 1,
    "rotation route paths are fixed and unique",
)

require(
    route.count("server.Post(") == 2,
    "both credential mutation endpoints use POST",
)

require(
    "server.Get(" not in route and
    "server.Put(" not in route and
    "server.Delete(" not in route,
    "credential mutation module exposes no alternate HTTP methods",
)

require(
    "prepare_rotation_authorized(" in route and
    "finalize_rotation_authorized(" in route,
    "route uses atomic authenticate-and-mutate runtime APIs",
)

require(
    "prepare_rotation(\n" not in route and
    "finalize_rotation(\n" not in route,
    "route does not use unauthenticated raw mutation APIs",
)

require(
    "const std::string*\n"
    "exact_authorization_header(" in route and
    "iterator !=\n"
    "            range.second" in route,
    "route rejects ambiguous duplicate Authorization fields",
)

require(
    "kMaximumAuthorizationHeaderBytes = 128" in route,
    "Authorization header has a fixed size bound",
)

require(
    "kInternalNodeCredentialRotationRateLimitRequests = 16" in header and
    "kInternalNodeCredentialRotationRateLimitTrackedPeers = 256" in header,
    "credential mutation rate limiter remains explicitly bounded",
)

require(
    "preauthenticate(" in route and
    "consume_rate_limit(" in route,
    "route authenticates before consuming mutation rate-limit budget",
)

preauth_pos = route.find(
    "preauthenticate("
)

rate_pos = route.find(
    "consume_rate_limit("
)

# Definitions appear earlier as well; verify handler preamble ordering using
# the actual call sites inside request_preamble().
preamble_pos = route.find(
    "bool request_preamble("
)

if preamble_pos >= 0:
    preamble = route[preamble_pos:]
    preauth_call = preamble.find(
        "!preauthenticate("
    )
    rate_call = preamble.find(
        "!consume_rate_limit("
    )

    require(
        preauth_call >= 0 and
        rate_call >= 0 and
        preauth_call < rate_call,
        "unauthenticated traffic cannot consume rotation rate-limit budget",
    )
else:
    require(
        False,
        "rotation request preamble exists",
    )

require(
    'response.set_header(\n'
    '        "Cache-Control",\n'
    '        "no-store"' in route,
    "credential responses disable caching",
)

require(
    "clear_internal_auth_token_text(" in route and
    "JsonCredentialWiper" in route and
    "SensitiveStringWiper" in route,
    "temporary plaintext credential copies are explicitly wiped",
)

require(
    "std::cerr" not in route and
    "std::cout" not in route,
    "credential rotation route does not log plaintext state",
)

require(
    "storage_node_credential_runtime_result_code(" not in route,
    "internal credential state/result codes are not reflected directly",
)

require(
    '"error", "unauthorized"' in route and
    '"error", "rotation_unavailable"' in route,
    "wire errors collapse authentication and internal persistence details",
)

require(
    "pending_.configured()\n"
    "            ? pending_match\n"
    "            : active_match;" in runtime,
    "runtime requires NEW specifically while finalization is pending",
)

require(
    "add_library(pqnas_internal_node_credential_rotation_routes STATIC" in cmake,
    "credential rotation route is a dedicated library",
)

require(
    "pqnas_internal_node_credential_rotation_routes\n"
    "        PUBLIC\n"
    "        pqnas_storage_node_credential_runtime" in cmake,
    "rotation route explicitly depends on credential runtime",
)

require(
    "pqnas_storage_node_credential_runtime\n"
    "        pqnas_internal_node_credential_rotation_routes\n"
    "        pqnas_node_deletion_service" in cmake,
    "pqnas_server links credential rotation production components",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print(
    "OK: node credential rotation production wiring guards passed"
)
