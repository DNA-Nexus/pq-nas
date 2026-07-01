#!/usr/bin/env bash
set -euo pipefail

die() {
  echo "pqnas-raid-root: $*" >&2
  exit 1
}

usage() {
  echo "Usage:" >&2
  echo "  pqnas-raid-root zap-disk <disk>" >&2
  echo "  pqnas-raid-root create-btrfs-partition <disk>" >&2
  echo "  pqnas-raid-root partprobe <disk>" >&2
  echo "  pqnas-raid-root wipefs <device>" >&2
  echo "  pqnas-raid-root mkfs-btrfs <single|raid1> <label> <device>..." >&2
  echo "  pqnas-raid-root mkdir-p <pool-mount-or-data-dir>" >&2
  echo "  pqnas-raid-root chown-pqnas <pool-data-dir>" >&2
  echo "  pqnas-raid-root chmod-0755 <pool-data-dir>" >&2
  echo "  pqnas-raid-root mount-label <label> <pool-mount>" >&2
  echo "  pqnas-raid-root mount-spec <LABEL=...|UUID=...> <pool-mount>" >&2
  echo "  pqnas-raid-root umount-pool <pool-mount>" >&2
  echo "  pqnas-raid-root rmdir-pool <pool-mount>" >&2
  echo "  pqnas-raid-root udev-settle" >&2
  echo "  pqnas-raid-root btrfs-device-scan" >&2
  echo "  pqnas-raid-root btrfs-scrub-start <pool-mount>" >&2
  echo "  pqnas-raid-root btrfs-device-add <partition> <pool-mount>" >&2
  echo "  pqnas-raid-root btrfs-device-remove <member-device> <pool-mount>" >&2
  echo "  pqnas-raid-root btrfs-balance-raid1 <pool-mount>" >&2
  echo "  pqnas-raid-root btrfs-balance-single-force <pool-mount>" >&2
  echo "  pqnas-raid-root btrfs-balance-force-profile <single|raid1> <pool-mount>" >&2
  exit 2
}

BTRFS="/usr/bin/btrfs"
MKFS_BTRFS="/usr/sbin/mkfs.btrfs"
SGDISK="/usr/sbin/sgdisk"
WIPEFS="/usr/sbin/wipefs"
PARTPROBE="/usr/sbin/partprobe"
UDEVADM="/usr/bin/udevadm"
MOUNT="/bin/mount"
UMOUNT="/bin/umount"
MKDIR="/bin/mkdir"
RMDIR="/bin/rmdir"
CHOWN="/bin/chown"
CHMOD="/bin/chmod"
READLINK="/usr/bin/readlink"
LSBLK="/usr/bin/lsblk"

for bin in "$BTRFS" "$MKFS_BTRFS" "$SGDISK" "$WIPEFS" "$PARTPROBE" "$UDEVADM" "$MOUNT" "$UMOUNT" "$MKDIR" "$RMDIR" "$CHOWN" "$CHMOD" "$READLINK" "$LSBLK"; do
  [ -x "$bin" ] || die "missing executable: $bin"
done

reject_bad_path_string() {
  local p="$1"
  [ -n "$p" ] || die "empty path"
  case "$p" in
    /*) ;;
    *) die "path must be absolute: $p" ;;
  esac
  case "$p" in
    *$'\n'*|*$'\r'*|*$'\t'*|*'..'*)
      die "path contains unsafe characters: $p"
      ;;
  esac
}

safe_pool_id() {
  local id="$1"
  [[ "$id" =~ ^[A-Za-z0-9._-]+$ ]]
}

safe_label() {
  local label="$1"
  [[ "$label" =~ ^PQNAS_[A-Za-z0-9._-]+$ ]]
}

safe_mount_spec() {
  local spec="$1"

  case "$spec" in
    LABEL=*)
      local label="${spec#LABEL=}"
      safe_label "$label"
      ;;
    UUID=*)
      local uuid="${spec#UUID=}"
      [[ "$uuid" =~ ^[A-Fa-f0-9-]{36}$ ]]
      ;;
    *)
      return 1
      ;;
  esac
}

normalize_path_may_not_exist() {
  local p="$1"
  "$READLINK" -m -- "$p"
}

require_block_device() {
  local dev="$1"
  reject_bad_path_string "$dev"

  case "$dev" in
    /dev/*) ;;
    *) die "device must be under /dev: $dev" ;;
  esac

  [ -b "$dev" ] || die "not a block device: $dev"
  printf '%s' "$dev"
}

require_unmounted_device_tree() {
  local dev="$1"
  local mounts

  mounts="$("$LSBLK" -nrpo MOUNTPOINT -- "$dev" 2>/dev/null | /bin/grep -v '^$' || true)"
  if [ -n "$mounts" ]; then
    die "device or its children appear mounted: $dev"
  fi
}

require_pool_mount() {
  local input="$1"
  local real
  local rest
  local pool_id

  reject_bad_path_string "$input"
  real="$(normalize_path_may_not_exist "$input")"

  case "$real" in
    /srv/pqnas/pools/*) ;;
    /srv/pqnas-test/pools/*) ;;
    /srv/pqnas-test-btrfs/pools/*) ;;
    *) die "pool mount is not under an allowed PQ-NAS pools root: $real" ;;
  esac

  rest="${real##*/pools/}"
  case "$rest" in
    */*) die "pool mount must be exact pool root: $real" ;;
  esac

  pool_id="$rest"
  safe_pool_id "$pool_id" || die "unsafe pool id: $pool_id"

  printf '%s' "$real"
}

require_pool_or_data_path() {
  local input="$1"
  local real
  local pool_root
  local tail

  reject_bad_path_string "$input"
  real="$(normalize_path_may_not_exist "$input")"

  case "$real" in
    */data)
      pool_root="${real%/data}"
      require_pool_mount "$pool_root" >/dev/null
      ;;
    *)
      require_pool_mount "$real" >/dev/null
      ;;
  esac

  tail="${real##*/pools/}"
  case "$tail" in
    */data|[A-Za-z0-9._-]*) ;;
    *) die "path is not a pool root or pool data dir: $real" ;;
  esac

  printf '%s' "$real"
}

require_pool_data_path() {
  local input="$1"
  local real
  local pool_root

  reject_bad_path_string "$input"
  real="$(normalize_path_may_not_exist "$input")"

  case "$real" in
    */data) ;;
    *) die "path must be exact pool data dir: $real" ;;
  esac

  pool_root="${real%/data}"
  require_pool_mount "$pool_root" >/dev/null

  printf '%s' "$real"
}

ACTION="${1:-}"
[ -n "$ACTION" ] || usage

case "$ACTION" in
  zap-disk)
    [ "$#" -eq 2 ] || usage
    DEV="$(require_block_device "$2")"
    require_unmounted_device_tree "$DEV"
    exec "$SGDISK" --zap-all "$DEV"
    ;;

  create-btrfs-partition)
    [ "$#" -eq 2 ] || usage
    DEV="$(require_block_device "$2")"
    require_unmounted_device_tree "$DEV"
    exec "$SGDISK" -n 1:0:0 -t 1:8300 -c 1:PQNAS_BTRFS "$DEV"
    ;;

  partprobe)
    [ "$#" -eq 2 ] || usage
    DEV="$(require_block_device "$2")"
    exec "$PARTPROBE" "$DEV"
    ;;

  wipefs)
    [ "$#" -eq 2 ] || usage
    DEV="$(require_block_device "$2")"
    require_unmounted_device_tree "$DEV"
    exec "$WIPEFS" -a "$DEV"
    ;;

  mkfs-btrfs)
    [ "$#" -ge 4 ] || usage
    MODE="$2"
    LABEL="$3"
    shift 3

    case "$MODE" in
      single|raid1) ;;
      *) die "unsupported mkfs mode: $MODE" ;;
    esac

    safe_label "$LABEL" || die "unsafe label: $LABEL"

    if [ "$MODE" = "raid1" ] && [ "$#" -lt 2 ]; then
      die "raid1 mkfs requires at least two devices"
    fi

    DEVICES=()
    for dev in "$@"; do
      DEV="$(require_block_device "$dev")"
      require_unmounted_device_tree "$DEV"
      DEVICES+=("$DEV")
    done

    if [ "$MODE" = "raid1" ]; then
      exec "$MKFS_BTRFS" -f -d raid1 -m raid1 -L "$LABEL" "${DEVICES[@]}"
    fi

    exec "$MKFS_BTRFS" -f -L "$LABEL" "${DEVICES[@]}"
    ;;

  mkdir-p)
    [ "$#" -eq 2 ] || usage
    TARGET="$(require_pool_or_data_path "$2")"
    exec "$MKDIR" -p "$TARGET"
    ;;

  chown-pqnas)
    [ "$#" -eq 2 ] || usage
    TARGET="$(require_pool_data_path "$2")"
    exec "$CHOWN" pqnas:pqnas "$TARGET"
    ;;

  chmod-0755)
    [ "$#" -eq 2 ] || usage
    TARGET="$(require_pool_data_path "$2")"
    exec "$CHMOD" 0755 "$TARGET"
    ;;

  mount-label)
    [ "$#" -eq 3 ] || usage
    LABEL="$2"
    MOUNTPOINT="$(require_pool_mount "$3")"
    safe_label "$LABEL" || die "unsafe label: $LABEL"
    exec "$MOUNT" -t btrfs "LABEL=$LABEL" "$MOUNTPOINT"
    ;;

  mount-spec)
    [ "$#" -eq 3 ] || usage
    SPEC="$2"
    MOUNTPOINT="$(require_pool_mount "$3")"
    safe_mount_spec "$SPEC" || die "unsafe mount spec: $SPEC"
    exec "$MOUNT" -t btrfs "$SPEC" "$MOUNTPOINT"
    ;;

  umount-pool)
    [ "$#" -eq 2 ] || usage
    MOUNTPOINT="$(require_pool_mount "$2")"
    exec "$UMOUNT" "$MOUNTPOINT"
    ;;

  rmdir-pool)
    [ "$#" -eq 2 ] || usage
    MOUNTPOINT="$(require_pool_mount "$2")"
    exec "$RMDIR" "$MOUNTPOINT"
    ;;

  udev-settle)
    [ "$#" -eq 1 ] || usage
    exec "$UDEVADM" settle
    ;;

  btrfs-device-scan)
    [ "$#" -eq 1 ] || usage
    exec "$BTRFS" device scan
    ;;

  btrfs-scrub-start)
    [ "$#" -eq 2 ] || usage
    MOUNTPOINT="$(require_pool_mount "$2")"
    exec "$BTRFS" scrub start "$MOUNTPOINT"
    ;;

  btrfs-device-add)
    [ "$#" -eq 3 ] || usage
    DEV="$(require_block_device "$2")"
    MOUNTPOINT="$(require_pool_mount "$3")"
    exec "$BTRFS" device add "$DEV" "$MOUNTPOINT"
    ;;

  btrfs-device-remove)
    [ "$#" -eq 3 ] || usage
    DEV="$(require_block_device "$2")"
    MOUNTPOINT="$(require_pool_mount "$3")"
    exec "$BTRFS" device remove "$DEV" "$MOUNTPOINT"
    ;;

  btrfs-balance-raid1)
    [ "$#" -eq 2 ] || usage
    MOUNTPOINT="$(require_pool_mount "$2")"
    exec "$BTRFS" balance start -dconvert=raid1 -mconvert=raid1 "$MOUNTPOINT"
    ;;

  btrfs-balance-single-force)
    [ "$#" -eq 2 ] || usage
    MOUNTPOINT="$(require_pool_mount "$2")"
    exec "$BTRFS" balance start --force -dconvert=single -mconvert=single -sconvert=single "$MOUNTPOINT"
    ;;

  btrfs-balance-force-profile)
    [ "$#" -eq 3 ] || usage
    PROFILE="$2"
    MOUNTPOINT="$(require_pool_mount "$3")"

    case "$PROFILE" in
      single)
        exec "$BTRFS" balance start --force -dconvert=single -mconvert=single -sconvert=single "$MOUNTPOINT"
        ;;
      raid1)
        exec "$BTRFS" balance start --force -dconvert=raid1 -mconvert=raid1 "$MOUNTPOINT"
        ;;
      *)
        die "unsupported balance profile: $PROFILE"
        ;;
    esac
    ;;

  *)
    usage
    ;;
esac
