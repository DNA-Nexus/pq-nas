#!/usr/bin/env python3
from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

ROOT = Path(__file__).resolve().parents[2]

main = (ROOT / "server/src/main.cpp").read_text()
production_tu = read_main_translation_unit_text(ROOT)
authz = (ROOT / "server/src/authz.cc").read_text()
strict_cookie = (ROOT / "server/include/strict_session_cookie.h").read_text()
put = (ROOT / "server/src/routes/routes_files_put.inc").read_text()
routes_v5 = (ROOT / "server/src/routes/routes_v5.cc").read_text()
routes_v5_h = (ROOT / "server/include/routes_v5.h").read_text()

def require(cond, msg):
    if not cond:
        print("FAIL:", msg)
        raise SystemExit(1)

# RT-013: every legacy pqnas_session authorization path must use the same
# duplicate-header / duplicate-name fail-closed parser.
require(
    "extract_strict_pqnas_session_cookie(" in strict_cookie and
    "equal_range(" in strict_cookie and
    "duplicate target names enable cookie tossing" in strict_cookie,
    "strict pqnas_session parser security contract missing",
)
require(
    '#include "strict_session_cookie.h"' in main and
    '#include "strict_session_cookie.h"' in authz,
    "legacy session auth must include the shared strict cookie parser",
)
# Security: main.cpp compiles local .inc implementation blocks, so negative
# wiring assertions must cover the expanded production translation unit.
require(
    "extract_named_cookie_value" not in production_tu and
    "extract_cookie_value" not in authz,
    "legacy first-match cookie parsers must not survive RT-013",
)
require(
    main.count("pqnas::extract_strict_pqnas_session_cookie(") == 3,
    "all three main.cpp pqnas_session auth paths must use strict parser",
)
require(
    authz.count("pqnas::extract_strict_pqnas_session_cookie(") == 4,
    "all four authz.cc pqnas_session auth paths must use strict parser",
)

# Security: an unconfigured composition root must fail closed.
require(
    "g_legacy_users_auth_allowed{false}" in authz,
    "legacy users.json auth policy must default to denied",
)

# Process role must explicitly configure the legacy user/admin authority.
require(
    "set_legacy_users_auth_allowed(" in main and
    "!cluster_config.is_node()" in main,
    "server role must configure legacy users.json admin auth policy",
)

# Both cookie and bearer legacy-admin paths must fail closed on Node.
require(
    "if (!legacy_users_auth_allowed())" in authz,
    "authz cookie/admin helpers need Storage Node backstop",
)

admin_fn = main[
    main.index("static bool require_admin_auth_users_actor("):
    main.index("static std::string random_b64url", main.index("static bool require_admin_auth_users_actor("))
]
require(
    "legacy_users_auth_allowed()" in admin_fn,
    "legacy admin bearer path must consult Node backstop",
)

# Legacy Files core includes search and many filesystem-mutating routes.
require(
    '''if (!cluster_config.is_node()) {
#include "routes/routes_files_core.inc"
    }''' in main,
    "legacy Files core routes must not register on Storage Node",
)

# Chunked upload is another legacy filesystem route outside files_core.
chunk = main[
    main.index("// ---- Chunked Upload API"):
    main.index("// ============================================================================", main.index("// ---- Chunked Upload API"))
]
require(
    "if (!cluster_config.is_node()) {" in chunk and
    "register_chunked_upload_routes(srv, uploads_ctx);" in chunk,
    "chunked upload routes must be Node role-gated",
)

# PUT itself is legacy and must be gated.
require(
    put.startswith(
        "// Security: the legacy PUT route uses local users.json authorization"
    ),
    "legacy PUT route must have explicit Node role gate",
)
require(
    "if (!cluster_config.is_node()) {\n// ---- Files API (user storage) ----" in put,
    "legacy PUT route must not register on Node",
)

# The migrated archive/blob family must remain available and projection-backed.
require(
    put.count("register_file_version_archive_blob_routes(") == 1,
    "archive/blob routes must have exactly one registration",
)
require(
    "if (cluster_config.is_node()) {" in put and
    "require_storage_user_request_auth(" in put and
    "resolve_at_revision(" in put,
    "Storage archive/blob route must retain projection/revision authorization",
)

# The unmigrated version/admin route families must be behind a Node gate.
require(
    '''if (!cluster_config.is_node()) {

    // ---- File version read routes ----''' in put,
    "unmigrated file-version/admin route block must be Node role-gated",
)

for registration in (
    "register_file_version_read_routes(",
    "register_file_version_manage_routes(",
    "register_file_version_restore_routes(",
    "register_snapshot_browse_routes(",
    "register_snapshot_create_routes(",
    "register_snapshot_restore_routes(",
    "register_admin_user_profile_routes(",
    "register_user_avatar_routes(",
    "register_apps_manage_routes(",
    "register_admin_user_lifecycle_routes(",
):
    require(
        registration in put,
        f"expected guarded legacy registration missing: {registration}",
    )

require(
    put.rstrip().endswith(
        "} // !cluster_config.is_node(): legacy file/admin routes"
    ),
    "legacy post-archive Node role gate must cover file tail",
)


# Normal legacy user cookie/Bearer authorization must fail closed on Node.
user_cookie_fn = main[
    main.index("static bool require_user_cookie_users_actor("):
    main.index("// ----- Cleanup-only session identity")
]
require(
    "legacy_users_auth_allowed()" in user_cookie_fn,
    "legacy user cookie auth must consult Storage Node backstop",
)

bearer_fn = main[
    main.index("static bool try_user_bearer_users_actor("):
    main.index("// ----- Mixed gate:", main.index("static bool try_user_bearer_users_actor("))
]
require(
    "legacy_users_auth_allowed()" in bearer_fn,
    "legacy user bearer auth must consult Storage Node backstop",
)

# RT-002 cleanup remains possible, but without consulting Node-local users.json.
require(
    "require_cleanup_user_cookie" in routes_v5_h,
    "RoutesV5Context must expose cleanup-only cookie authentication",
)

for marker, next_marker in (
    ('srv.Post("/api/v5/app_pair/cancel"', 'srv.Get("/api/v5/app_devices"'),
    ('srv.Post("/api/v5/app_devices/revoke"', '// Authenticated browser-only QR renderer.'),
):
    start = routes_v5.index(marker)
    end = routes_v5.index(next_marker, start)
    segment = routes_v5[start:end]

    require(
        "ctx.require_cleanup_user_cookie" in segment,
        f"cleanup route must use cleanup-only cookie auth: {marker}",
    )
    require(
        "ctx.require_user_cookie" not in segment,
        f"cleanup route must not use legacy users.json cookie auth: {marker}",
    )

require(
    "v5.require_cleanup_user_cookie =" in main and
    "require_cleanup_session_cookie_identity(" in main,
    "production must wire cleanup-only signed-cookie identity",
)

print("OK: RT-001 legacy Node route-gating guards passed")
