#!/usr/bin/env python3

from pathlib import Path

main = Path(
    "server/src/main.cpp"
).read_text()

header = Path(
    "server/src/routes/routes_admin_cluster_nodes.h"
).read_text()

route = Path(
    "server/src/routes/routes_admin_cluster_nodes.cpp"
).read_text()

cmake = Path(
    "CMakeLists.txt"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


require(
    '#include "cluster/node_credential_rotation_service.h"' in main and
    '#include "cluster/node_credential_rotation_https.h"' in main,
    "Director includes credential rotation service and HTTPS transport",
)

require(
    "NodeCredentialRotationServiceConfig" in main and
    "director_node_credential_rotation_service" in main,
    "Director creates a process-lifetime rotation service",
)

require(
    ".node_registry =\n"
    "            &director_node_registry;" in main and
    ".credential_registry =\n"
    "            &director_node_credential_registry;" in main,
    "rotation service uses authoritative Director registries",
)

require(
    ".coordination_mutex =\n"
    "            cluster_placement_mutex;" in main,
    "rotation service shares the Director placement coordination mutex",
)


require(
    ".tls_trust_store =\n"
    "            &director_node_tls_trust_store;" in main,
    "rotation service uses the authoritative Director TLS trust store",
)

require(
    "make_https_node_credential_rotation_prepare_transport()" in main and
    "make_https_node_credential_rotation_finalize_transport()" in main,
    "production rotation service uses hardened HTTPS transports",
)

credential_load_position = main.find(
    "director_node_credential_registry.load("
)

service_position = main.find(
    "NodeCredentialRotationServiceConfig"
)


trust_load_position = main.find(
    "director_node_tls_trust_store.load("
)

require(
    trust_load_position >= 0 and
    service_position > trust_load_position,
    "rotation service is constructed only after TLS trust-store load",
)

require(
    credential_load_position >= 0 and
    service_position > credential_load_position,
    "rotation service is constructed only after credential registry load",
)

require(
    "rotation_in_progress" in header,
    "admin Node route context exposes rotation busy state",
)

require(
    "admin_cluster_node_context.rotation_in_progress =" in main and
    "director_node_credential_rotation_service" in main,
    "production admin Node routes query the live rotation service",
)

require(
    "query_rotation_in_progress(" in route and
    route.count(
        "if (rotation_busy) {"
    ) == 3,
    "UPDATE, TLS migration and DELETE fail closed on active rotation",
)

require(
    "failed to check node credential rotation state" in route,
    "busy-state provider failures fail lifecycle mutations closed",
)

require(
    "pqnas_node_credential_rotation_https" in cmake and
    "target_link_libraries(pqnas_server PRIVATE" in cmake,
    "server links Director credential rotation HTTPS composition",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print(
    "OK: Director node credential rotation production wiring guards passed"
)
