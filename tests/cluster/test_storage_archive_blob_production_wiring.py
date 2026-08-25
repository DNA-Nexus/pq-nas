from pathlib import Path

from production_translation_unit import (
    read_main_translation_unit_text,
)

root = Path(__file__).resolve().parents[2]

routes = (
    root /
    "server/src/routes/routes_files_put.inc"
).read_text(encoding="utf-8")

context_header = (
    root /
    "server/src/routes/routes_file_versions_archive_blob.h"
).read_text(encoding="utf-8")

route_impl = (
    root /
    "server/src/routes/routes_file_versions_archive_blob.cpp"
).read_text(encoding="utf-8")

bounded_provider_header = (
    root /
    "server/include/bounded_file_content_provider.h"
).read_text(encoding="utf-8")

main = (
    root /
    "server/src/main.cpp"
).read_text(encoding="utf-8")

# Security: negative production-wiring assertions must inspect the
# implementation .inc files compiled into main.cpp as well as the
# literal main.cpp source. This closes historical RT-011.
production_tu = read_main_translation_unit_text()

errors = []


def require(condition: bool, message: str) -> None:
    if not condition:
        errors.append(message)


start_marker = (
    "// ---- File Manager file-version archive/blob routes ----"
)

end_marker = (
    "// ---- File version read routes ----"
)

start = routes.find(start_marker)
end = routes.find(end_marker, start)

require(
    start >= 0 and end > start,
    "archive/blob production wiring block must exist",
)

archive_block = (
    routes[start:end]
    if start >= 0 and end > start
    else ""
)

require(
    archive_block.count(
        "register_file_version_archive_blob_routes("
    ) == 1,
    "archive/blob routes must be registered exactly once",
)

require(
    "if (cluster_config.is_node())" in archive_block,
    "archive/blob wiring must separate Storage Node role",
)

role_split = archive_block.split(
    "        } else {",
    1
)

require(
    len(role_split) == 2,
    "archive/blob wiring must have Node and Director branches",
)

node_branch = role_split[0] if role_split else ""
director_branch = role_split[1] if len(role_split) == 2 else ""

require(
    "require_storage_user_request_auth(" in node_branch,
    "Storage archive/blob must use Storage request auth",
)

require(
    "request_auth_context()" in node_branch,
    "Storage archive/blob must use composed request-auth context",
)

require(
    "resolve_at_revision(" in node_branch,
    "Storage root must be bound to authenticated revision",
)

require(
    "authenticated_projection_revision" in node_branch,
    "Storage branch must propagate authenticated revision",
)

require(
    "require_user_auth_users_actor(" not in node_branch,
    "Storage branch must not use Director auth",
)

require(
    "user_dir_for_fp(" not in node_branch,
    "Storage branch must not use Director filesystem placement",
)

require(
    "require_user_auth_users_actor(" in director_branch,
    "Director compatibility auth must remain",
)

require(
    "user_dir_for_fp(" in director_branch,
    "Director compatibility root resolution must remain",
)

require(
    "*authenticated_projection_revision = 0;" in director_branch,
    "Director branch must explicitly carry no Storage revision",
)

require(
    "std::uint64_t* authenticated_projection_revision" in
        context_header,
    "auth callback must expose authenticated revision",
)

require(
    "std::uint64_t authenticated_projection_revision" in
        context_header,
    "root callback must receive authenticated revision",
)

require(
    route_impl.count(
        "authenticated_projection_revision"
    ) >= 4,
    "route handlers must propagate auth revision to root resolution",
)

# RT-012: authenticated version blob downloads must remain streaming and
# bounded. Never restore a whole-file std::string response on this route.
blob_route_start = route_impl.find(
    'srv.Get("/api/v4/files/versions/blob"'
)

blob_route = (
    route_impl[blob_route_start:]
    if blob_route_start >= 0
    else ""
)

require(
    blob_route_start >= 0,
    "version blob route must exist",
)

require(
    "install_bounded_file_content_provider(" in blob_route,
    "version blob route must install bounded content provider",
)

require(
    "std::istreambuf_iterator<char>" not in blob_route,
    "version blob route must not read the entire blob into memory",
)

require(
    "res.set_content(std::move(body)" not in blob_route,
    "version blob route must not return a whole-file response body",
)

require(
    'res.set_header(\n                "Accept-Ranges",\n                "bytes"' in blob_route,
    "version blob route must expose byte-range streaming",
)

require(
    "response.set_content_provider(" in bounded_provider_header,
    "bounded file helper must use httplib content provider",
)

require(
    "kBoundedFileContentProviderBufferBytes" in bounded_provider_header and
    "64 * 1024" in bounded_provider_header,
    "bounded file helper must retain a 64 KiB buffer ceiling",
)

require(
    "sink.write(" in bounded_provider_header,
    "bounded file helper must stream provider chunks to DataSink",
)

# Security invariant: Storage file wiring must not open browser handoff.
require(
    "register_storage_handoff_consume_routes(" not in production_tu,
    "Storage handoff consume must remain production-disabled",
)

require(
    "register_storage_handoff_preauth_routes(" not in production_tu,
    "Storage handoff preauth must remain production-disabled",
)

require(
    "register_internal_storage_handoff_routes(" not in production_tu,
    "internal Storage handoff must remain production-disabled",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")
    raise SystemExit(1)

print("OK: Storage archive/blob production wiring guards passed")
