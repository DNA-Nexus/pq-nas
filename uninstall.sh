#!/usr/bin/env bash
set -euo pipefail

# DNA-Nexus Server / PQ-NAS Safe Uninstaller
#
# Removes:
# - systemd unit
# - installed binaries
# - shipped /opt/pqnas static/runtime assets
# - installer-generated nginx site config, only if it contains our marker
#
# Keeps:
# - /srv/pqnas or configured PQNAS_ROOT data
# - /etc/pqnas configuration
# - users, shares, workspaces, storage pools, and user files
#
# Usage:
#   sudo ./uninstall.sh
#
# This is intentionally conservative. It will not delete NAS data.

say() { echo "[*] $*"; }
warn() { echo "[!] $*" >&2; }
die() { echo "[ERROR] $*" >&2; exit 1; }

need_root() {
  if [[ "${EUID:-$(id -u)}" -ne 0 ]]; then
    die "Run as root: sudo $0"
  fi
}

have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

trim_copy() {
  local s="${1:-}"

  s="${s#"${s%%[![:space:]]*}"}"
  s="${s%"${s##*[![:space:]]}"}"

  printf '%s\n' "$s"
}

strip_matching_quotes() {
  local s="${1:-}"

  if [[ "$s" == \"*\" && "$s" == *\" ]]; then
    s="${s#\"}"
    s="${s%\"}"
  elif [[ "$s" == \'*\' && "$s" == *\' ]]; then
    s="${s#\'}"
    s="${s%\'}"
  fi

  printf '%s\n' "$s"
}

read_pqnas_root_from_env() {
  local env="/etc/pqnas/pqnas.env"
  local line=""
  local value=""

  if [[ ! -f "$env" ]]; then
    echo ""
    return 0
  fi

  line="$(grep -E '^[[:space:]]*PQNAS_ROOT[[:space:]]*=' "$env" | tail -n 1 || true)"
  if [[ -z "$line" ]]; then
    echo ""
    return 0
  fi

  value="${line#*=}"
  value="$(trim_copy "$value")"
  value="$(strip_matching_quotes "$value")"

  echo "$value"
}

service_exists() {
  if ! have_cmd systemctl; then
    return 1
  fi

  systemctl cat pqnas.service >/dev/null 2>&1 && return 0

  systemctl list-unit-files pqnas.service --no-legend --no-pager 2>/dev/null \
    | grep -q '^pqnas\.service' && return 0

  return 1
}

stop_disable_service() {
  if ! have_cmd systemctl; then
    say "systemctl not found; skipping service stop/disable."
    return 0
  fi

  if service_exists; then
    say "Stopping pqnas.service if running..."
    systemctl stop pqnas.service >/dev/null 2>&1 || true

    say "Disabling pqnas.service..."
    systemctl disable --now pqnas.service >/dev/null 2>&1 || true
  else
    say "pqnas.service not registered with systemd; skipping service stop/disable."
  fi
}

kill_leftover_processes() {
  if ! have_cmd pgrep || ! have_cmd pkill; then
    say "pgrep/pkill not found; skipping process cleanup."
    return 0
  fi

  if pgrep -f '^/usr/local/bin/pqnas_server([[:space:]]|$)' >/dev/null 2>&1; then
    say "Killing installed pqnas_server process from /usr/local/bin..."
    pkill -f '^/usr/local/bin/pqnas_server([[:space:]]|$)' >/dev/null 2>&1 || true
  fi

  local leftovers=""
  leftovers="$(pgrep -af 'pqnas_server' || true)"
  if [[ -n "$leftovers" ]]; then
    warn "Other pqnas_server-looking processes are still running. Leaving them untouched:"
    echo "$leftovers" >&2
  fi
}

remove_systemd_unit() {
  local unit="/etc/systemd/system/pqnas.service"

  if [[ -f "$unit" ]]; then
    say "Removing systemd unit: $unit"
    rm -f "$unit"

    if have_cmd systemctl; then
      say "Reloading systemd..."
      systemctl daemon-reload >/dev/null 2>&1 || true
      systemctl reset-failed >/dev/null 2>&1 || true
    fi
  else
    say "systemd unit not found: $unit"
  fi
}

remove_binaries() {
  local bin_dir="/usr/local/bin"
  local files=(
    "$bin_dir/pqnas_server"
    "$bin_dir/pqnas_keygen"
    "$bin_dir/pqnas_server.bak"
    "$bin_dir/pqnas_keygen.bak"
    "$bin_dir/pqnas_server.new"
    "$bin_dir/pqnas_keygen.new"
  )

  say "Removing installed binaries from $bin_dir if present..."
  for f in "${files[@]}"; do
    if [[ -e "$f" || -L "$f" ]]; then
      rm -f "$f"
      say "  removed: $f"
    fi
  done
}

remove_opt_assets() {
  local static="/opt/pqnas/static"
  local dnalib="/opt/pqnas/lib/dna/libdna_lib.so"
  local dnadir="/opt/pqnas/lib/dna"
  local libroot="/opt/pqnas/lib"
  local optroot="/opt/pqnas"

  if [[ -d "$static" ]]; then
    say "Removing shipped static assets: $static"
    rm -rf "$static"
  else
    say "Static assets not found: $static"
  fi

  if [[ -f "$dnalib" || -L "$dnalib" ]]; then
    say "Removing shipped DNA engine library: $dnalib"
    rm -f "$dnalib"
  else
    say "DNA engine library not found: $dnalib"
  fi

  if [[ -d "$dnadir" ]] && rmdir "$dnadir" 2>/dev/null; then
    say "Removed empty directory: $dnadir"
  fi

  if [[ -d "$libroot" ]] && rmdir "$libroot" 2>/dev/null; then
    say "Removed empty directory: $libroot"
  fi

  if [[ -d "$optroot" ]] && rmdir "$optroot" 2>/dev/null; then
    say "Removed empty directory: $optroot"
  fi
}

nginx_remove_site_if_ours() {
  if ! have_cmd nginx || ! have_cmd systemctl; then
    say "nginx or systemctl not found; skipping nginx cleanup."
    return 0
  fi

  local candidates=(
    "/etc/nginx/sites-available/pqnas"
    "/etc/nginx/conf.d/pqnas.conf"
    "/etc/nginx/sites-enabled/pqnas"
  )

  local found=""
  for c in "${candidates[@]}"; do
    if [[ -f "$c" || -L "$c" ]]; then
      found="$c"
      break
    fi
  done

  if [[ -z "$found" ]]; then
    say "No pqnas nginx config found."
    return 0
  fi

  if ! grep -q 'PQ-NAS nginx reverse proxy' "$found" 2>/dev/null; then
    warn "Found nginx config $found, but it does not look installer-generated. Leaving it untouched."
    return 0
  fi

  say "Removing nginx config: $found"
  rm -f "$found"

  if [[ -L "/etc/nginx/sites-enabled/pqnas" || -e "/etc/nginx/sites-enabled/pqnas" ]]; then
    say "Removing nginx enabled link: /etc/nginx/sites-enabled/pqnas"
    rm -f "/etc/nginx/sites-enabled/pqnas"
  fi

  say "Testing nginx configuration..."
  if nginx -t >/dev/null 2>&1; then
    say "Reloading nginx..."
    systemctl reload nginx >/dev/null 2>&1 || true
  else
    warn "nginx -t failed after removal; not reloading. Please check nginx config."
  fi
}

print_keep_notes() {
  local root=""
  root="$(read_pqnas_root_from_env)"

  echo
  if [[ -n "$root" ]]; then
    say "Kept DATA unchanged: $root"
  else
    say "Kept DATA unchanged: /srv/pqnas"
  fi

  echo
  say "Kept CONFIG unchanged: /etc/pqnas/"
  say "If you reinstall later, existing config, users, shares, and data may still be present."
}

confirm() {
  echo
  echo "This will perform a SAFE uninstall of DNA-Nexus Server / PQ-NAS:"
  echo "  - stop and disable pqnas.service"
  echo "  - remove /etc/systemd/system/pqnas.service"
  echo "  - remove /usr/local/bin/pqnas_server and pqnas_keygen"
  echo "  - remove shipped assets under /opt/pqnas"
  echo "  - optionally remove installer-generated nginx pqnas site config"
  echo
  echo "It will NOT delete:"
  echo "  - /srv/pqnas or your configured PQNAS_ROOT data"
  echo "  - /etc/pqnas configuration"
  echo "  - users, shares, workspaces, storage pools, or uploaded files"
  echo
  read -r -p "Type exactly: UNINSTALL  > " ans
  [[ "$ans" == "UNINSTALL" ]] || die "Confirmation did not match. Nothing done."
}

main() {
  need_root
  confirm

  stop_disable_service
  kill_leftover_processes
  remove_systemd_unit
  remove_binaries
  remove_opt_assets
  nginx_remove_site_if_ours

  echo
  say "Safe uninstall complete."
  print_keep_notes
}

main "$@"
