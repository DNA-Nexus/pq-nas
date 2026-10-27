#!/usr/bin/env bash
set -euo pipefail

case "${1:-}" in
  ""|--browser) ;;
  *) echo "Usage: $0 [--browser]" >&2; exit 2 ;;
esac

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$repo_root"
test_build_dir="$(mktemp -d)"
trap 'rm -rf "$test_build_dir"' EXIT
compiler="${CXX:-c++}"

node --check apps/bundled/photogallery/src/www/app.js
node --check apps/bundled/photogallery/src/www/gallery_search_stream.js
node --test tests/photogallery/test_geo_scope.cjs tests/photogallery/test_map_i18n.cjs tests/photogallery/test_map_integration.cjs tests/photogallery/search_stream.test.cjs
python3 tests/cluster/test_standalone_apps_callback_lifetime.py
python3 tests/cluster/test_legacy_file_routes_role_gating.py

"$compiler" -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iserver/src \
  tests/file_location_index/test_file_location_scope.cpp \
  server/src/file_location_index.cpp -lsqlite3 \
  -o "$test_build_dir/file_location_scope"
"$test_build_dir/file_location_scope"

"$compiler" -std=c++17 -Wall -Wextra -Wpedantic -Werror -pthread \
  -Iserver/src -Iserver/include -Iserver/third_party \
  tests/gallery_search/test_gallery_tree_scan.cpp \
  server/src/file_location_index.cpp server/src/gallery_meta.cpp -lsqlite3 \
  -o "$test_build_dir/gallery_tree_scan"
"$test_build_dir/gallery_tree_scan"

if [[ "${1:-}" == --browser ]]; then
  node tests/photogallery/progressive_search.browser.cjs
fi
