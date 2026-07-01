#!/usr/bin/env bash
set -euo pipefail

SMARTCTL="/usr/sbin/smartctl"

die() {
  echo "ERROR: $*" >&2
  exit 2
}

# Security: only root should run the real smartctl binary from this wrapper.
# pqnas gets sudo access to this wrapper, not unrestricted smartctl.
if [ "$(id -u)" != "0" ]; then
  die "must run as root"
fi

[ -x "$SMARTCTL" ] || die "smartctl not found at $SMARTCTL"

# Security: allow installer validation without opening arbitrary smartctl usage.
if [ "$#" -eq 1 ] && [ "$1" = "--version" ]; then
  exec "$SMARTCTL" --version
fi

is_safe_whole_disk() {
  local dev="$1"

  # Security: require real block devices and reject normal files / shell tricks.
  [ -b "$dev" ] || return 1

  # Security: accept only whole-disk style device paths, not partitions.
  # This protects against using the root wrapper on arbitrary paths or partition
  # nodes such as /dev/nvme0n1p1 or /dev/mmcblk0p1.
  [[ "$dev" =~ ^/dev/sd[a-z]{1,2}$ ]] && return 0
  [[ "$dev" =~ ^/dev/vd[a-z]{1,2}$ ]] && return 0
  [[ "$dev" =~ ^/dev/xvd[a-z]{1,2}$ ]] && return 0
  [[ "$dev" =~ ^/dev/hd[a-z]{1,2}$ ]] && return 0
  [[ "$dev" =~ ^/dev/nvme[0-9]+n[0-9]+$ ]] && return 0
  [[ "$dev" =~ ^/dev/mmcblk[0-9]+$ ]] && return 0

  return 1
}

run_info_probe() {
  local mode="$1"
  local dev="$2"

  is_safe_whole_disk "$dev" || die "unsafe or unsupported device path: $dev"

  # Security: only allow JSON read-only probe modes used by drive_health.cc.
  exec "$SMARTCTL" "$mode" -j "$dev"
}

run_selftest() {
  local kind="$1"
  local dev="$2"

  is_safe_whole_disk "$dev" || die "unsafe or unsupported device path: $dev"

  case "$kind" in
    short|long) ;;
    *) die "unsupported self-test type: $kind" ;;
  esac

  # Security: only allow the UI-supported SMART self-test start commands.
  exec "$SMARTCTL" -t "$kind" "$dev"
}

case "$#" in
  3)
    if { [ "$1" = "-a" ] || [ "$1" = "-i" ]; } && [ "$2" = "-j" ]; then
      run_info_probe "$1" "$3"
    fi

    # Security: allow only the exact SMART self-test form used by DNA-Nexus:
    #   pqnas-smartctl -t short|long /dev/<whole-disk>
    # This avoids opening arbitrary smartctl root commands.
    if [ "$1" = "-t" ]; then
      run_selftest "$2" "$3"
    fi
    ;;
esac

die "unsupported smartctl invocation"
