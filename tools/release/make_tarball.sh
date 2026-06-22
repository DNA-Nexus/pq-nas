#!/usr/bin/env bash
set -euo pipefail

# PQ-NAS release tarball builder
# Usage:
#   ./tools/release/make_tarball.sh 0.9.0
# Output:
#   /tmp/pqnas-release/pqnas-<ver>-linux-x86_64.tar.gz

if [[ $# -lt 1 ]]; then
  echo "Usage: $0 <version> [--skip-server-build] [--skip-build]"
  echo "Example:"
  echo "  $0 1.1.8"
  echo "  $0 1.1.8 --skip-server-build"
  echo
  echo "Environment alternatives:"
  echo "  PQNAS_RELEASE_SKIP_SERVER_BUILD=1 $0 1.1.8"
  echo "  PQNAS_RELEASE_SKIP_BUILD=1        $0 1.1.8"
  exit 1
fi

is_truthy() {
  case "${1:-}" in
    1|true|TRUE|yes|YES|on|ON) return 0 ;;
    *) return 1 ;;
  esac
}

VER="$1"
shift

ARCH="x86_64"
SKIP_SERVER_BUILD="${PQNAS_RELEASE_SKIP_SERVER_BUILD:-0}"
SKIP_ALL_BUILD="${PQNAS_RELEASE_SKIP_BUILD:-0}"
SKIP_WASM_BUILD="${PQNAS_RELEASE_SKIP_WASM_BUILD:-0}"
ALLOW_STALE_SERVER="${PQNAS_RELEASE_ALLOW_STALE_SERVER:-0}"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-server-build)
      SKIP_SERVER_BUILD=1
      ;;
    --skip-build|--no-build)
      SKIP_ALL_BUILD=1
      ;;
    --build)
      SKIP_SERVER_BUILD=0
      SKIP_ALL_BUILD=0
      ;;
    *)
      echo "ERROR: Unknown option: $1"
      echo "Usage: $0 <version> [--skip-server-build] [--skip-build]"
      exit 1
      ;;
  esac
  shift
done
OUTDIR="/tmp/pqnas-release"
STAGE="$OUTDIR/pqnas"
TARBALL="$OUTDIR/pqnas-${VER}-linux-${ARCH}.tar.gz"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
VERSION_H="$REPO_ROOT/server/src/version.h"

if [[ ! -f "$VERSION_H" ]]; then
  echo "ERROR: Missing version header: $VERSION_H"
  exit 1
fi

APP_VER="$(sed -n 's/^#define[[:space:]]\+PQNAS_VERSION[[:space:]]\+"\([^"]\+\)".*/\1/p' "$VERSION_H" | head -n1)"

if [[ -z "$APP_VER" ]]; then
  echo "ERROR: Failed to parse PQNAS_VERSION from $VERSION_H"
  exit 1
fi

# Release argument must match PQNAS_VERSION because the update manifest,
# tarball filename, and compiled server version must describe the same build.
if [[ "$VER" != "$APP_VER" ]]; then
  echo "ERROR: Release version mismatch."
  echo "  make_tarball argument: $VER"
  echo "  PQNAS_VERSION:          $APP_VER"
  echo "Update server/src/version.h or pass the matching version."
  exit 17
fi

REL_ROOT="$REPO_ROOT/tools/release"
CLEAN_CONFIG_DIR="$REL_ROOT/config"
SYSTEMD_DIR="$REL_ROOT/systemd"
UPDATE_MANIFEST_TEMPLATE="$REL_ROOT/update_manifest.template.json"
RESTORE_JOB_SRC="$REPO_ROOT/server/src/storage/snapshots/pqnas_restore_job.sh"
DRIVE_LOCATE_WRAPPER_SRC="$REPO_ROOT/server/src/storage/pqnas_drive_locate_root.sh"

# DNA Connect runtime for alerts
# Override from environment if needed:
#   DNA_ALERT_CLI_SRC=/path/to/dna-connect-cli DNA_ALERT_SO_SRC=/path/to/libdna.so ./tools/release/make_tarball.sh 0.9.0
DNA_ALERT_CLI_SRC="${DNA_ALERT_CLI_SRC:-$REPO_ROOT/third_party/dna-runtime/linux-x64/dna-connect-cli}"
DNA_ALERT_SO_SRC="${DNA_ALERT_SO_SRC:-$REPO_ROOT/third_party/dna-runtime/linux-x64/libdna.so}"

echo "[*] Repo root: $REPO_ROOT"
echo "[*] Release:   $VER"
echo "[*] App ver:   $APP_VER"
echo "[*] Stage:     $STAGE"
echo "[*] Output:    $TARBALL"

rm -rf "$OUTDIR"
mkdir -p "$STAGE"

# ---- 1) Build binaries ----
if is_truthy "$SKIP_ALL_BUILD"; then
  echo "[*] Skipping CMake build completely (--skip-build)."
  echo "[*] Using existing binaries from: $REPO_ROOT/build/bin"
elif is_truthy "$SKIP_SERVER_BUILD"; then
  echo "[*] Skipping pqnas_server build (--skip-server-build)."
  echo "[*] Building helper tools only: pqnas_keygen + nodus-cli + pqnas_opaque_helper..."
  cmake --build "$REPO_ROOT/build" --target pqnas_keygen nodus-cli pqnas_opaque_helper_rust
else
  echo "[*] Building pqnas_server + pqnas_keygen + nodus-cli + pqnas_opaque_helper..."
  cmake --build "$REPO_ROOT/build" --target pqnas_server pqnas_keygen nodus-cli pqnas_opaque_helper_rust
fi

test -x "$REPO_ROOT/build/bin/pqnas_server"
test -x "$REPO_ROOT/build/bin/pqnas_keygen"
test -x "$REPO_ROOT/build/bin/nodus-cli"
test -x "$REPO_ROOT/build/bin/pqnas_opaque_helper_rust"

# If the server build was skipped, protect against accidentally packaging an
# old server binary after server/src/version.h was bumped.
if { is_truthy "$SKIP_ALL_BUILD" || is_truthy "$SKIP_SERVER_BUILD"; } && ! is_truthy "$ALLOW_STALE_SERVER"; then
  if command -v strings >/dev/null 2>&1; then
    if ! strings "$REPO_ROOT/build/bin/pqnas_server" | grep -F -- "$APP_VER" >/dev/null 2>&1; then
      echo "ERROR: Existing build/bin/pqnas_server does not appear to contain PQNAS_VERSION=$APP_VER"
      echo "This usually means version.h was changed but pqnas_server was not rebuilt."
      echo
      echo "Fix:"
      echo "  cmake --build build -j"
      echo "  tools/release/make_tarball.sh $VER --skip-server-build"
      echo
      echo "Override only if you know the binary is correct:"
      echo "  PQNAS_RELEASE_ALLOW_STALE_SERVER=1 tools/release/make_tarball.sh $VER --skip-server-build"
      exit 20
    fi
  fi
fi

# ---- 2) NOTE: repo config may be "dirty" during development ----
# We do NOT ship $REPO_ROOT/config anymore. We always ship tools/release/config.
# So: warn only (useful reminder), but do not fail the release build.
if [[ -d "$REPO_ROOT/config" ]]; then
  if rg -n --hidden --no-messages \
      '"fingerprint"\s*:\s*"|pqnas_session|cookie|token|secret|sk_b64|private|owner_fp|expires_at|downloads' \
      "$REPO_ROOT/config" >/dev/null 2>&1; then
    echo "[!] WARNING: $REPO_ROOT/config contains dev/state data (fingerprints/tokens/etc)."
    echo "[!] This is OK during development because release tarballs use: $CLEAN_CONFIG_DIR"
  fi
fi

# ---- 3) Ensure release template config exists + contains required files ----
if [[ ! -d "$CLEAN_CONFIG_DIR" ]]; then
  echo "ERROR: Missing clean config templates: $CLEAN_CONFIG_DIR"
  echo "Create it and add sanitized defaults (admin_settings.json, policy.json, users.json, shares.json, pools.json)."
  exit 2
fi

required=(admin_settings.json policy.json users.json shares.json pools.json)
for f in "${required[@]}"; do
  if [[ ! -f "$CLEAN_CONFIG_DIR/$f" ]]; then
    echo "ERROR: Missing required config template: $CLEAN_CONFIG_DIR/$f"
    exit 2
  fi
done

# Extra guard: templates must not contain fingerprints/tokens either.
if rg -n --hidden --no-messages \
    '"fingerprint"\s*:\s*"[0-9a-f]{32,}"|pqnas_session|cookie|token|secret|sk_b64|private' \
    "$CLEAN_CONFIG_DIR" >/dev/null 2>&1; then
  echo "ERROR: $CLEAN_CONFIG_DIR contains secrets/dev data. Templates must be sanitized."
  echo "Remove fingerprints/tokens/keys and ship safe defaults only."
  exit 2
fi

# ---- 4) Stage package layout expected by installer ----
echo "[*] Staging files..."

# Top-level installer entrypoint (for end users)
if [[ -f "$REPO_ROOT/install.sh" ]]; then
  install -m 0755 "$REPO_ROOT/install.sh" "$STAGE/install.sh"
else
  echo "[!] Missing $REPO_ROOT/install.sh (top-level installer launcher)."
fi

# Top-level SAFE uninstaller entrypoint (keeps /srv/pqnas data + /etc/pqnas config)
if [[ -f "$REPO_ROOT/uninstall.sh" ]]; then
  install -m 0755 "$REPO_ROOT/uninstall.sh" "$STAGE/uninstall.sh"
else
  echo "[!] Missing $REPO_ROOT/uninstall.sh (safe uninstaller)."
fi

# Binaries at package root
install -m 0755 "$REPO_ROOT/build/bin/pqnas_server" "$STAGE/pqnas_server"
install -m 0755 "$REPO_ROOT/build/bin/pqnas_keygen" "$STAGE/pqnas_keygen"
install -m 0755 "$REPO_ROOT/build/bin/nodus-cli" "$STAGE/nodus-cli"

install -d "$STAGE/libexec/pqnas"
install -m 0755 "$REPO_ROOT/build/bin/pqnas_opaque_helper_rust" "$STAGE/libexec/pqnas/pqnas_opaque_helper"

test -x "$STAGE/libexec/pqnas/pqnas_opaque_helper" || {
  echo "ERROR: OPAQUE helper did not stage"
  false
}

# DNA engine shared library (needed by /api/v4/verify)
DNA_SRC="$REPO_ROOT/server/third_party/dna/lib/linux/x64/libdna_lib.so"
if [[ -f "$DNA_SRC" ]]; then
  install -d "$STAGE/lib/dna"
  install -m 0755 "$DNA_SRC" "$STAGE/lib/dna/libdna_lib.so"
else
  echo "ERROR: Missing DNA lib: $DNA_SRC"
  echo "Build or fetch libdna_lib.so before making a release."
  exit 3
fi

# DNA Connect alert runtime (required)
if [[ ! -f "$DNA_ALERT_CLI_SRC" ]]; then
  echo "ERROR: Missing DNA Connect CLI: $DNA_ALERT_CLI_SRC"
  echo "Set DNA_ALERT_CLI_SRC or place runtime in third_party/dna-runtime/linux-x64/"
  exit 13
fi

if [[ ! -f "$DNA_ALERT_SO_SRC" ]]; then
  echo "ERROR: Missing DNA Connect shared lib: $DNA_ALERT_SO_SRC"
  echo "Set DNA_ALERT_SO_SRC or place runtime in third_party/dna-runtime/linux-x64/"
  exit 14
fi

install -d "$STAGE/runtime/dna"
install -m 0755 "$DNA_ALERT_CLI_SRC" "$STAGE/runtime/dna/dna-connect-cli"
install -m 0755 "$DNA_ALERT_SO_SRC"  "$STAGE/runtime/dna/libdna.so"

test -x "$STAGE/runtime/dna/dna-connect-cli" || {
  echo "ERROR: staged dna-connect-cli missing or not executable"
  exit 15
}
test -f "$STAGE/runtime/dna/libdna.so" || {
  echo "ERROR: staged libdna.so missing"
  exit 16
}

# systemd units (restore jobs + helpers)
if [[ -d "$SYSTEMD_DIR" ]]; then
  install -d "$STAGE/systemd"
  rsync -a --delete \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    "$SYSTEMD_DIR/" "$STAGE/systemd/"
else
  echo "ERROR: Missing systemd dir: $SYSTEMD_DIR"
  echo "Create tools/release/systemd and add pqnas-restore@.service + pqnas-ok.service + pqnas-fail.service"
  exit 5
fi

# Snapshot restore job script (installed to /usr/local/lib/pqnas/)
if [[ -f "$RESTORE_JOB_SRC" ]]; then
  install -d "$STAGE/lib/pqnas"
  install -m 0755 "$RESTORE_JOB_SRC" "$STAGE/lib/pqnas/pqnas_restore_job.sh"
else
  echo "[!] Snapshot restore job not found: $RESTORE_JOB_SRC"
  echo "[!] This is OK only if you intentionally ship without snapshot-restore support."
fi

OPAQUE_BROWSER_DIR="$REPO_ROOT/tools/opaque_browser_client"
OPAQUE_STATIC_DIR="$REPO_ROOT/server/src/static/opaque-test"

if [[ -f "$OPAQUE_BROWSER_DIR/Cargo.toml" ]]; then
  if is_truthy "$SKIP_WASM_BUILD"; then
    echo "[*] Skipping OPAQUE WASM build via PQNAS_RELEASE_SKIP_WASM_BUILD=1."
  else
    if ! command -v wasm-pack >/dev/null 2>&1; then
      echo "ERROR: wasm-pack not found. Install: cargo install wasm-pack --locked"
      false
    fi

    echo "[*] Building OPAQUE browser WASM client..."
    wasm-pack build "$OPAQUE_BROWSER_DIR" --target web --out-dir pkg

    install -d "$OPAQUE_STATIC_DIR"
    install -m 0644 "$OPAQUE_BROWSER_DIR/pkg/pqnas_opaque_browser_client.js" "$OPAQUE_STATIC_DIR/pqnas_opaque_browser_client.js"
    install -m 0644 "$OPAQUE_BROWSER_DIR/pkg/pqnas_opaque_browser_client_bg.wasm" "$OPAQUE_STATIC_DIR/pqnas_opaque_browser_client_bg.wasm"
  fi

  test -f "$OPAQUE_STATIC_DIR/pqnas_opaque_browser_client.js" || {
    echo "ERROR: OPAQUE browser JS missing"
    false
  }

  test -f "$OPAQUE_STATIC_DIR/pqnas_opaque_browser_client_bg.wasm" || {
    echo "ERROR: OPAQUE browser WASM missing"
    false
  }
fi

# Static web assets (package-mode)
# Update Center apply helper assets
# Package layout expected by installer:
#   <asset_root>/libexec/pqnas/pqnas_update_apply.py
#   <asset_root>/libexec/pqnas/pqnas_update_apply_root.sh
install -d "$STAGE/libexec/pqnas"

install -m 0755 \
  "$REPO_ROOT/server/src/updates/pqnas_update_apply.py" \
  "$STAGE/libexec/pqnas/pqnas_update_apply.py"

install -m 0755 \
  "$REPO_ROOT/server/src/updates/pqnas_update_apply_root.sh" \
  "$STAGE/libexec/pqnas/pqnas_update_apply_root.sh"

test -f "$STAGE/libexec/pqnas/pqnas_update_apply.py" || {
  echo "ERROR: Update Center apply helper did not stage"
  exit 1
}

test -f "$STAGE/libexec/pqnas/pqnas_update_apply_root.sh" || {
  echo "ERROR: Update Center root apply wrapper did not stage"
  exit 1
}

# First-admin bootstrap helper.
# Package layout expected by installer:
#   <asset_root>/libexec/pqnas/pqnas-first-admin
FIRST_ADMIN_HELPER_SRC="$REPO_ROOT/tools/runtime/pqnas-first-admin"
if [[ ! -f "$FIRST_ADMIN_HELPER_SRC" ]]; then
  echo "ERROR: Missing first-admin helper: $FIRST_ADMIN_HELPER_SRC"
  exit 23
fi

install -m 0755 \
  "$FIRST_ADMIN_HELPER_SRC" \
  "$STAGE/libexec/pqnas/pqnas-first-admin"

test -x "$STAGE/libexec/pqnas/pqnas-first-admin" || {
  echo "ERROR: first-admin helper did not stage"
  exit 24
}

# Notifications + Warnings worker.
# Package layout expected by installer:
#   <asset_root>/libexec/pqnas/pqnas_notify.py
NOTIFY_WORKER_SRC="$REPO_ROOT/tools/runtime/pqnas_notify.py"
if [[ ! -f "$NOTIFY_WORKER_SRC" ]]; then
  echo "ERROR: Missing notification worker: $NOTIFY_WORKER_SRC"
  exit 25
fi

install -m 0755 \
  "$NOTIFY_WORKER_SRC" \
  "$STAGE/libexec/pqnas/pqnas_notify.py"

test -x "$STAGE/libexec/pqnas/pqnas_notify.py" || {
  echo "ERROR: notification worker did not stage"
  exit 26
}

# Drive-bay locate root wrapper.
# Package layout expected by installer:
#   <asset_root>/libexec/pqnas/pqnas-drive-locate
if [[ ! -f "$DRIVE_LOCATE_WRAPPER_SRC" ]]; then
  echo "ERROR: Missing drive locate wrapper: $DRIVE_LOCATE_WRAPPER_SRC"
  exit 21
fi

install -m 0755   "$DRIVE_LOCATE_WRAPPER_SRC"   "$STAGE/libexec/pqnas/pqnas-drive-locate"

test -x "$STAGE/libexec/pqnas/pqnas-drive-locate" || {
  echo "ERROR: Drive locate root wrapper did not stage"
  exit 22
}

# Staging update manifest at tarball root:
#   <tarball>/pqnas/update_manifest.json
if [[ ! -f "$UPDATE_MANIFEST_TEMPLATE" ]]; then
  echo "ERROR: Missing update manifest template: $UPDATE_MANIFEST_TEMPLATE"
  exit 18
fi

echo "[*] Staging update manifest..."
tmp_manifest="$(mktemp)"
sed \
  -e "s/__PQNAS_VERSION__/${APP_VER}/g" \
  -e "s/__PQNAS_TARBALL_VERSION__/${VER}/g" \
  -e "s/__PQNAS_ARCH__/${ARCH}/g" \
  "$UPDATE_MANIFEST_TEMPLATE" > "$tmp_manifest"

python3 -m json.tool "$tmp_manifest" >/dev/null
install -m 0644 "$tmp_manifest" "$STAGE/update_manifest.json"
rm -f "$tmp_manifest"

test -f "$STAGE/update_manifest.json" || {
  echo "ERROR: update_manifest.json did not stage"
  exit 19
}

# Copies: server/src/static/*  ->  <tarball>/pqnas/static/*
rsync -a --delete \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  "$REPO_ROOT/server/src/static/" "$STAGE/static/"

# Wiki documentation exposed through the product UI:
#   /static/wiki/index.html
# Source of truth remains docs/wiki/.
WIKI_SRC="$REPO_ROOT/docs/wiki"
if [[ -d "$WIKI_SRC" ]]; then
  echo "[*] Staging wiki documentation to static/wiki..."
  install -d "$STAGE/static/wiki"
  rsync -a --delete \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    "$WIKI_SRC/" "$STAGE/static/wiki/"

  test -f "$STAGE/static/wiki/index.html" || {
    echo "ERROR: wiki did not stage to $STAGE/static/wiki (missing index.html)"
    exit 91
  }
else
  echo "[!] Wiki docs not found: $WIKI_SRC"
  echo "[!] Release will ship without /static/wiki/."
fi

# HARD GUARD: fail release if static didn't stage
test -f "$STAGE/static/app.js" || {
  echo "ERROR: static assets did not stage to $STAGE/static (missing app.js)"
  echo "REPO_ROOT=$REPO_ROOT"
  echo "STAGE=$STAGE"
  ls -la "$REPO_ROOT/server/src/static" | head -n 80 || true
  ls -la "$STAGE" | head -n 120 || true
  exit 90
}

# Inject app version from server/src/version.h into static assets.
# app.html / app.js should contain: __PQNAS_VERSION__
if [[ -f "$STAGE/static/app.html" ]]; then
  tmp="$(mktemp)"
  sed "s/__PQNAS_VERSION__/${APP_VER}/g" "$STAGE/static/app.html" > "$tmp"
  install -m 0644 "$tmp" "$STAGE/static/app.html"
  rm -f "$tmp"
else
  echo "[!] Missing staged static HTML: $STAGE/static/app.html"
fi

if [[ -f "$STAGE/static/app.js" ]]; then
  tmp="$(mktemp)"
  sed "s/__PQNAS_VERSION__/${APP_VER}/g" "$STAGE/static/app.js" > "$tmp"
  install -m 0644 "$tmp" "$STAGE/static/app.js"
  rm -f "$tmp"
else
  echo "[!] Missing staged static JS: $STAGE/static/app.js"
fi

# ---- 4.x) Build bundled app zips from src (source of truth) ----
# Ensures release tarball ships fresh <id>-<version>.zip artifacts.
if [[ -x "$REPO_ROOT/tools/build_all_bundled_zips.sh" ]]; then
  echo "[*] Building bundled app zips..."
  "$REPO_ROOT/tools/build_all_bundled_zips.sh"
else
  echo "ERROR: Missing zip builder: $REPO_ROOT/tools/build_all_bundled_zips.sh"
  echo "Add it, or run your app zip build step before making a tarball."
  exit 11
fi

# HARD GUARD: fail if any app directory has src/ but no zip
missing_zips=0
for appdir in "$REPO_ROOT/apps/bundled"/*; do
  [[ -d "$appdir" ]] || continue
  if [[ -d "$appdir/src" ]]; then
    if ! ls "$appdir"/*.zip >/dev/null 2>&1; then
      echo "ERROR: App has src/ but no zip: $appdir"
      missing_zips=1
    fi
  fi
done
[[ "$missing_zips" -eq 0 ]] || exit 12

# Bundled apps (zips + any bundled folders)
if [[ -d "$REPO_ROOT/apps/bundled" ]]; then
  rsync -a --delete \
    --include '*/' \
    --include '*.zip' \
    --exclude '*' \
    "$REPO_ROOT/apps/bundled/" "$STAGE/bundled/"
else
  mkdir -p "$STAGE/bundled"
fi

# Clean default config (IMPORTANT: from tools/release/config, not repo config/)
rsync -a --delete \
  --exclude '__pycache__/' \
  --exclude '*.pyc' \
  "$CLEAN_CONFIG_DIR/" "$STAGE/config/"

# Installer (textual wizard)
if [[ -d "$REPO_ROOT/tools/installer" ]]; then
  rsync -a --delete \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    "$REPO_ROOT/tools/installer/" "$STAGE/installer/"
else
  echo "[!] Missing tools/installer (expected installer script)."
  mkdir -p "$STAGE/installer"
fi

# Docs (optional)
if [[ -d "$REPO_ROOT/docs" ]]; then
  rsync -a --delete \
    --exclude '__pycache__/' \
    --exclude '*.pyc' \
    "$REPO_ROOT/docs/" "$STAGE/docs/"
fi

# README / LICENSE
[[ -f "$REPO_ROOT/README.md" ]] && install -m 0644 "$REPO_ROOT/README.md" "$STAGE/README.md"
[[ -f "$REPO_ROOT/LICENSE" ]] && install -m 0644 "$REPO_ROOT/LICENSE" "$STAGE/LICENSE"

# ---- 5) Clean junk that should never ship ----
echo "[*] Cleaning __pycache__ and *.pyc..."
find "$STAGE" -type d -name '__pycache__' -prune -exec rm -rf {} + || true
find "$STAGE" -type f -name '*.pyc' -delete || true

# ---- 6) Create tarball ----
echo "[*] Creating tarball..."
(
  cd "$OUTDIR"
  tar -czf "$(basename "$TARBALL")" pqnas
)

echo "[*] Done."
ls -lh "$TARBALL"

echo
echo "Test extract:"
echo "  rm -rf /tmp/pqnas-test && mkdir -p /tmp/pqnas-test"
echo "  tar -xzf '$TARBALL' -C /tmp/pqnas-test"
echo "  ls -la /tmp/pqnas-test/pqnas"
echo
echo "Expected manifest/binaries/runtime inside tarball:"
echo "  /tmp/pqnas-test/pqnas/update_manifest.json"
echo "  /tmp/pqnas-test/pqnas/pqnas_server"
echo "  /tmp/pqnas-test/pqnas/pqnas_keygen"
echo "  /tmp/pqnas-test/pqnas/nodus-cli"
echo "  /tmp/pqnas-test/pqnas/runtime/dna/dna-connect-cli"
echo "  /tmp/pqnas-test/pqnas/runtime/dna/libdna.so"
echo "  /tmp/pqnas-test/pqnas/libexec/pqnas/pqnas-first-admin"