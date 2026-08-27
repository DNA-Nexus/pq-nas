#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

composition_path = (
    ROOT
    / "server/src/routes/routes_admin_stats_badges.inc"
)

source = composition_path.read_text(
    encoding="utf-8"
)

if_marker = "if (!cluster_config.is_node()) {"
end_marker = "} // !cluster_config.is_node(): legacy workspace routes"

if_pos = source.find(if_marker)
end_pos = source.find(end_marker)

failures = []

if if_pos < 0:
    failures.append(
        "legacy workspace route gate missing"
    )

if end_pos < 0:
    failures.append(
        "legacy workspace route gate end missing"
    )

deps = (
    (
        "pqnas::WorkspaceFileRouteDeps ws_file_deps;",
        "ws_file_deps",
        "pqnas::register_workspace_file_routes(srv, ws_file_deps);",
    ),
    (
        "pqnas::WorkspaceExternalInviteRouteDeps ws_external_invite_deps;",
        "ws_external_invite_deps",
        "pqnas::register_workspace_external_invite_routes(srv, ws_external_invite_deps);",
    ),
    (
        "pqnas::WorkspaceExternalSessionRouteDeps ws_external_session_deps;",
        "ws_external_session_deps",
        "pqnas::register_workspace_external_session_routes(srv, ws_external_session_deps);",
    ),
)

for declaration, name, registration in deps:
    count = source.count(declaration)

    if count != 1:
        failures.append(
            f"{name}: expected exactly one declaration, found {count}"
        )
        continue

    decl_pos = source.find(declaration)

    if if_pos >= 0 and decl_pos > if_pos:
        failures.append(
            f"{name}: dependency bundle is declared inside "
            "the temporary route-registration block"
        )

    reg_pos = source.find(registration)

    if reg_pos < 0:
        failures.append(
            f"{name}: route registration missing"
        )
        continue

    if (
        if_pos >= 0
        and end_pos >= 0
        and not (if_pos < reg_pos < end_pos)
    ):
        failures.append(
            f"{name}: route registration escaped role gate"
        )

main_source = (
    ROOT / "server/src/main.cpp"
).read_text(
    encoding="utf-8"
)

include_marker = (
    '#include "routes/routes_admin_stats_badges.inc"'
)

listen_marker = "srv.listen(listen_host, LISTEN_PORT);"

include_pos = main_source.find(include_marker)
listen_pos = main_source.find(listen_marker)

if include_pos < 0:
    failures.append(
        "main.cpp: route composition include missing"
    )

if listen_pos < 0:
    failures.append(
        "main.cpp: server listen anchor missing"
    )

if (
    include_pos >= 0
    and listen_pos >= 0
    and include_pos >= listen_pos
):
    failures.append(
        "main.cpp: workspace dependency composition "
        "does not precede server lifetime"
    )

if failures:
    print(
        "FAIL: workspace route dependency lifetime guard"
    )

    for failure in failures:
        print(f"  - {failure}")

    raise SystemExit(1)

print(
    "PASS: workspace route dependency bundles "
    "outlive registered handlers"
)
