#!/usr/bin/env bash
set -euo pipefail

die() {
  echo "pqnas-btrfs-status: $*" >&2
  exit 1
}

usage() {
  echo "Usage:" >&2
  echo "  pqnas-btrfs-status filesystem-show <mount>" >&2
  echo "  pqnas-btrfs-status filesystem-df <mount>" >&2
  echo "  pqnas-btrfs-status filesystem-df-bytes <mount>" >&2
  echo "  pqnas-btrfs-status filesystem-usage <mount>" >&2
  echo "  pqnas-btrfs-status filesystem-usage-bytes <mount>" >&2
  echo "  pqnas-btrfs-status device-stats <mount>" >&2
  echo "  pqnas-btrfs-status scrub-status <mount>" >&2
  echo "  pqnas-btrfs-status balance-status <mount>" >&2
  exit 2
}

BTRFS="/usr/bin/btrfs"
READLINK="/usr/bin/readlink"

[ -x "$BTRFS" ] || die "missing btrfs binary"
[ -x "$READLINK" ] || die "missing readlink binary"

ACTION="${1:-}"
MOUNT="${2:-}"

[ -n "$ACTION" ] || usage
[ -n "$MOUNT" ] || usage
[ "$#" -eq 2 ] || usage

reject_bad_path_string() {
  local p="$1"

  [ -n "$p" ] || die "empty path"

  case "$p" in
    /*) ;;
    *) die "mount path must be absolute: $p" ;;
  esac

  case "$p" in
    *$'\n'*|*$'\r'*|*$'\t'*|*'..'*)
      die "mount path contains unsafe characters: $p"
      ;;
  esac
}

require_allowed_mount() {
  local input="$1"
  local real

  reject_bad_path_string "$input"

  real="$("$READLINK" -f -- "$input")" || die "mount path does not exist: $input"

  case "$real" in
    /srv/pqnas|/srv/pqnas/*) ;;
    /srv/pqnas-test|/srv/pqnas-test/*) ;;
    /srv/pqnas-test-btrfs|/srv/pqnas-test-btrfs/*) ;;
    *) die "mount path is not under an allowed PQ-NAS root: $real" ;;
  esac

  printf '%s' "$real"
}

MOUNT_REAL="$(require_allowed_mount "$MOUNT")"

# Security: read-only helper for Btrfs status probes. This intentionally exposes
# only non-mutating btrfs status commands for PQ-NAS managed mount roots.
case "$ACTION" in
  filesystem-show)
    exec "$BTRFS" filesystem show "$MOUNT_REAL"
    ;;
  filesystem-df)
    exec "$BTRFS" filesystem df "$MOUNT_REAL"
    ;;
  filesystem-df-bytes)
    exec "$BTRFS" filesystem df -b "$MOUNT_REAL"
    ;;
  filesystem-usage)
    exec "$BTRFS" filesystem usage "$MOUNT_REAL"
    ;;
  filesystem-usage-bytes)
    exec "$BTRFS" filesystem usage -b "$MOUNT_REAL"
    ;;
  device-stats)
    exec "$BTRFS" device stats "$MOUNT_REAL"
    ;;
  scrub-status)
    exec "$BTRFS" scrub status "$MOUNT_REAL"
    ;;
  balance-status)
    exec "$BTRFS" balance status "$MOUNT_REAL"
    ;;
  *)
    usage
    ;;
esac
