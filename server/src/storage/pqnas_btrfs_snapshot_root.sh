#!/usr/bin/env bash
set -euo pipefail

die() {
  echo "pqnas-btrfs-snapshot: $*" >&2
  exit 1
}

usage() {
  echo "Usage:" >&2
  echo "  pqnas-btrfs-snapshot create-ro /srv/pqnas/data /srv/pqnas/.snapshots/data/<snapshot_id>" >&2
  echo "  pqnas-btrfs-snapshot create-ro /srv/pqnas/pools/<pool_id> /srv/pqnas/pools/<pool_id>/.snapshots/<snapshot_id>" >&2
  echo "  pqnas-btrfs-snapshot delete /srv/pqnas/.snapshots/data/<snapshot_id>" >&2
  echo "  pqnas-btrfs-snapshot delete /srv/pqnas/pools/<pool_id>/.snapshots/<snapshot_id>" >&2
  exit 2
}

BTRFS="/usr/bin/btrfs"
READLINK="/usr/bin/readlink"

[ -x "$BTRFS" ] || die "missing btrfs binary"
[ -x "$READLINK" ] || die "missing readlink binary"

ACTION="${1:-}"
[ -n "$ACTION" ] || usage

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

safe_snapshot_id() {
  local id="$1"
  [[ "$id" =~ ^[A-Za-z0-9._:-]+$ ]]
}

allowed_snapshot_parent_for_source() {
  local src_real="$1"

  # Security: allow only PQ-NAS managed snapshot layouts. Do not expose
  # arbitrary btrfs snapshot creation through sudo.
  if [ "$src_real" = "/srv/pqnas/data" ]; then
    printf '%s' "/srv/pqnas/.snapshots/data"
    return 0
  fi

  local rest="${src_real#/srv/pqnas/pools/}"
  [ "$rest" != "$src_real" ] || die "source is not an allowed PQ-NAS snapshot source"

  case "$rest" in
    */*) die "source must be exact pool root: $src_real" ;;
  esac

  safe_pool_id "$rest" || die "unsafe pool id: $rest"
  printf '%s' "/srv/pqnas/pools/$rest/.snapshots"
}

allowed_snapshot_parent_for_delete_target() {
  local target_parent="$1"

  if [ "$target_parent" = "/srv/pqnas/.snapshots/data" ]; then
    printf '%s' "/srv/pqnas/.snapshots/data"
    return 0
  fi

  case "$target_parent" in
    /srv/pqnas/pools/*/.snapshots) ;;
    *) die "delete target is not inside an allowed PQ-NAS snapshot root" ;;
  esac

  local pool_dir
  local pool_id
  pool_dir="$(dirname -- "$target_parent")"
  pool_id="$(basename -- "$pool_dir")"

  safe_pool_id "$pool_id" || die "unsafe pool id: $pool_id"
  printf '%s' "/srv/pqnas/pools/$pool_id/.snapshots"
}

require_real_path_equals() {
  local path="$1"
  local expected="$2"
  local real

  real="$("$READLINK" -f -- "$path")" || die "path does not exist: $path"
  [ "$real" = "$expected" ] || die "path must not resolve outside allowed location: $path"
}

require_btrfs_subvolume() {
  local path="$1"

  "$BTRFS" subvolume show "$path" >/dev/null 2>&1 || \
    die "path is not a btrfs subvolume: $path"
}

case "$ACTION" in
  create-ro)
    [ "$#" -eq 3 ] || usage

    SRC="$2"
    DST="$3"

    reject_bad_path_string "$SRC"
    reject_bad_path_string "$DST"

    SRC_REAL="$("$READLINK" -f -- "$SRC")" || die "source does not exist"
    SNAP_PARENT="$(allowed_snapshot_parent_for_source "$SRC_REAL")"

    DST_PARENT="$(dirname -- "$DST")"
    DST_BASE="$(basename -- "$DST")"

    safe_snapshot_id "$DST_BASE" || die "unsafe snapshot id: $DST_BASE"
    [ "$DST_PARENT" = "$SNAP_PARENT" ] || die "destination must be inside the allowed snapshot root"

    require_real_path_equals "$SNAP_PARENT" "$SNAP_PARENT"
    [ ! -e "$DST" ] || die "destination already exists"

    require_btrfs_subvolume "$SRC_REAL"

    # Security: service user may request snapshots, but root validates the
    # source/destination pair before creating a readonly btrfs snapshot.
    exec "$BTRFS" subvolume snapshot -r "$SRC_REAL" "$DST"
    ;;

  delete)
    [ "$#" -eq 2 ] || usage

    TARGET="$2"

    reject_bad_path_string "$TARGET"

    TARGET_PARENT="$(dirname -- "$TARGET")"
    TARGET_BASE="$(basename -- "$TARGET")"

    safe_snapshot_id "$TARGET_BASE" || die "unsafe snapshot id: $TARGET_BASE"

    EXPECTED_PARENT="$(allowed_snapshot_parent_for_delete_target "$TARGET_PARENT")"
    [ "$TARGET_PARENT" = "$EXPECTED_PARENT" ] || die "unexpected snapshot parent"

    require_real_path_equals "$EXPECTED_PARENT" "$EXPECTED_PARENT"
    require_real_path_equals "$TARGET" "$TARGET"
    require_btrfs_subvolume "$TARGET"

    # Security: deletion is restricted to one btrfs snapshot subvolume below a
    # PQ-NAS managed snapshot root.
    exec "$BTRFS" subvolume delete "$TARGET"
    ;;

  *)
    usage
    ;;
esac
