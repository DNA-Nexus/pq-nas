#!/usr/bin/env python3
"""Guard standalone Apps callbacks against dangling captures after role wiring.

The legacy Apps registration lives inside an if (!cluster_config.is_node())
block in routes_files_put.inc, but handlers run after that block exits.
Version helpers and the server-version string defined there must therefore be
captured by value throughout the callback chain stored in std::function.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "server/src/routes/routes_files_put.inc"
routes = SOURCE.read_text(encoding="utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        print("FAIL:", message)
        raise SystemExit(1)


start = routes.find("    const std::string PQNAS_SERVER_VERSION = PQNAS_VERSION;")
end = routes.find("register_apps_manage_routes(srv, apps_ctx);", start)
role_end = routes.find("} // !cluster_config.is_node(): legacy file/admin routes", end)

require(start >= 0 and end > start and role_end > end,
        "Apps registration must remain inside the legacy role-gated scope")

wiring = routes[start:end + len("register_apps_manage_routes(srv, apps_ctx);")]

# A capture by value at the outer level is not enough if an inner helper
# itself retains references to local variables from the registration scope.
for fragment in (
    "auto compare_app_versions = [app_version_component](",
    "auto app_server_version_ok = [compare_app_versions, server_version = PQNAS_SERVER_VERSION](",
    "return compare_app_versions(server_version, minServerVersion)",
    "auto app_compatibility_message = [server_version = PQNAS_SERVER_VERSION](",
    'item["server_version"] = server_version;',
):
    require(fragment in wiring, f"missing value-owned compatibility helper: {fragment}")

apply_start = wiring.find("auto apply_app_compatibility_fields = [")
apply_end = wiring.find("](json& item, const json& mani) {", apply_start)
require(apply_start >= 0 and apply_end > apply_start,
        "apply_app_compatibility_fields capture list missing")
captures = wiring[apply_start:apply_end]
for value in (
    "app_manifest_min_server_version",
    "app_server_version_ok",
    "app_compatibility_message",
    "server_version = PQNAS_SERVER_VERSION",
):
    require(value in captures,
            f"compatibility field helper does not own {value} by value")
require("&" not in captures,
        "compatibility field helper must not capture local helpers by reference")

for context_field, captured in (
    ("apply_app_compatibility_fields", "apply_app_compatibility_fields"),
    ("app_manifest_min_server_version", "app_manifest_min_server_version"),
    ("app_server_version_ok", "app_server_version_ok"),
    ("app_compatibility_message", "app_compatibility_message"),
):
    require(
        f"apps_ctx.{context_field} =\n            [{captured}](" in wiring,
        f"stored Apps callback {context_field} must capture its helper by value",
    )

# Both affected routes must continue to be registered; do not "fix" the
# crash by removing functionality or bypassing the compatibility check.
implementation = (
    ROOT / "server/src/routes/routes_apps_manage.cpp"
).read_text(encoding="utf-8")
require('srv.Get("/api/v4/apps/list"' in implementation,
        "Apps listing route missing")
require('srv.Post("/api/v4/apps/upload_install"' in implementation,
        "Apps ZIP upload route missing")
require("app_server_version_ok(minServerVersion)" in implementation,
        "Apps ZIP upload must retain server-version compatibility check")
require(
    wiring.count("register_apps_manage_routes(srv, apps_ctx);") == 1,
    "Apps routes must be registered exactly once",
)

print("OK: standalone Apps callbacks own scoped version helpers by value")
