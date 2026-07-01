#!/usr/bin/env bash
set -euo pipefail

# Security: do not let the unprivileged service environment influence root helper behavior.
unset BASH_ENV ENV CDPATH
export PATH="/usr/sbin:/usr/bin:/sbin:/bin"
umask 022

BTRFS="/usr/bin/btrfs"
SYSTEMCTL="/usr/bin/systemctl"
READLINK="/usr/bin/readlink"

die() {
  echo "pqnas-restore-root: $*" >&2
  exit 1
}

usage() {
  echo "Usage:" >&2
  echo "  pqnas-restore-root subvolume-show <allowed-path>" >&2
  echo "  pqnas-restore-root subvolume-create-snapshot-root <allowed-snapshot-root>" >&2
  echo "  pqnas-restore-root systemctl-status-pqnas" >&2
  echo "  pqnas-restore-root systemctl-show-restore <job-id>" >&2
  echo "  pqnas-restore-root systemctl-start-restore <job-id>" >&2
  echo "  pqnas-restore-root systemctl-start-marker <ok|fail>" >&2
  exit 2
}

for bin in "$BTRFS" "$SYSTEMCTL" "$READLINK"; do
  [ -x "$bin" ] || die "required executable missing: $bin"
done

# Security: reject control characters and traversal before any privileged path use.
reject_bad_string() {
  local value="$1"
  [ -n "$value" ] || die "empty argument rejected"

  case "$value" in
    *$'\n'*|*$'\r'*|*$'\t'*)
      die "control characters rejected"
      ;;
    *'..'*)
      die "path traversal rejected"
      ;;
  esac
}

# Security: job IDs become systemd template instances; accept only server-generated
# RJOB_<base64url> IDs so attacker-controlled unit names cannot reach systemctl.
safe_job_id() {
  local id="$1"
  [[ "$id" =~ ^RJOB_[A-Za-z0-9_-]{24}$ ]]
}

normalize_path_may_not_exist() {
  local p="$1"
  reject_bad_string "$p"
  case "$p" in
    /*) ;;
    *) die "path must be absolute: $p" ;;
  esac
  "$READLINK" -m -- "$p"
}

# Security: restore/snapshot btrfs probing is constrained to PQ-NAS managed roots.
require_allowed_pqnas_path() {
  local input="$1"
  local real
  real="$(normalize_path_may_not_exist "$input")"

  case "$real" in
    /srv/pqnas|/srv/pqnas/*|\
/srv/pqnas-test|/srv/pqnas-test/*|\
/srv/pqnas-test-btrfs|/srv/pqnas-test-btrfs/*)
      printf '%s' "$real"
      ;;
    *)
      die "path is outside allowed PQ-NAS roots: $real"
      ;;
  esac
}

# Security: root-created snapshot roots must stay under a .snapshots subtree.
require_snapshot_root_path() {
  local real
  real="$(require_allowed_pqnas_path "$1")"

  case "$real" in
    */.snapshots|*/.snapshots/*)
      printf '%s' "$real"
      ;;
    *)
      die "snapshot root must be under a .snapshots subtree: $real"
      ;;
  esac
}

restore_unit_from_job_id() {
  local job_id="$1"
  safe_job_id "$job_id" || die "unsafe restore job id: $job_id"
  printf 'pqnas-restore@%s.service' "$job_id"
}

ACTION="${1:-}"
case "$ACTION" in
  subvolume-show)
    [ "$#" -eq 2 ] || usage
    PATH_ARG="$(require_allowed_pqnas_path "$2")"
    exec "$BTRFS" subvolume show "$PATH_ARG"
    ;;

  subvolume-create-snapshot-root)
    [ "$#" -eq 2 ] || usage
    PATH_ARG="$(require_snapshot_root_path "$2")"
    exec "$BTRFS" subvolume create "$PATH_ARG"
    ;;

  systemctl-status-pqnas)
    [ "$#" -eq 1 ] || usage
    exec "$SYSTEMCTL" status pqnas.service
    ;;

  systemctl-show-restore)
    [ "$#" -eq 2 ] || usage
    UNIT="$(restore_unit_from_job_id "$2")"
    exec "$SYSTEMCTL" show "$UNIT" \
      -p ActiveState -p SubState -p Result -p ExecMainStatus -p ExecMainCode
    ;;

  systemctl-start-restore)
    [ "$#" -eq 2 ] || usage
    UNIT="$(restore_unit_from_job_id "$2")"
    exec "$SYSTEMCTL" start "$UNIT"
    ;;

  systemctl-start-marker)
    [ "$#" -eq 2 ] || usage
    case "$2" in
      ok) exec "$SYSTEMCTL" start pqnas-ok.service ;;
      fail) exec "$SYSTEMCTL" start pqnas-fail.service ;;
      *) die "unsupported marker service: $2" ;;
    esac
    ;;

  *)
    usage
    ;;
esac
