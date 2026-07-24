#!/usr/bin/env bash
set -euo pipefail

SERVICE_NAME="${SERVICE_NAME:-pqnas.service}"
CONFIG_DIR="${CONFIG_DIR:-/srv/pqnas/config}"
DATA_DIR="${DATA_DIR:-/srv/pqnas/data}"
PQNAS_USER="${PQNAS_USER:-pqnas}"
PQNAS_GROUP="${PQNAS_GROUP:-pqnas}"

# Fixed development snapshot root. Do not allow environment overrides
# to redirect this destructive cleanup to an arbitrary directory.
SNAPSHOT_DIR="/srv/pqnas/.snapshots"
SNAPSHOT_DATA_DIR="$SNAPSHOT_DIR/data"
SNAPSHOT_HELPER="/usr/local/sbin/pqnas-btrfs-snapshot"
BTRFS="/usr/bin/btrfs"

USERS_JSON="$CONFIG_DIR/users.json"
WORKSPACES_JSON="$CONFIG_DIR/workspaces.json"
SHARES_JSON="$CONFIG_DIR/shares.json"
APP_AUTH_JSON="$CONFIG_DIR/app_auth.json"
PASSWORD_CREDENTIALS_JSON="$CONFIG_DIR/password_credentials.json"
OPAQUE_CREDENTIALS_JSON="$CONFIG_DIR/opaque_credentials.json"
OPAQUE_ENROLLMENTS_JSON="$CONFIG_DIR/opaque_enrollments.json"

die() {
    echo "[reset] ERROR: $*" >&2
    exit 1
}

log() {
    echo "[reset] $*"
}

require_cmd() {
    command -v "$1" >/dev/null 2>&1 || die "missing required command: $1"
}

[[ "${EUID}" -eq 0 ]] || die "run as root (sudo)"
require_cmd jq
require_cmd realpath
require_cmd find
[[ -x "$BTRFS" ]] || die "missing btrfs binary: $BTRFS"
[[ -x "$SNAPSHOT_HELPER" ]] ||
    die "missing snapshot helper: $SNAPSHOT_HELPER"
[[ -d "$CONFIG_DIR" ]] || die "missing config dir: $CONFIG_DIR"
[[ -d "$DATA_DIR" ]] || die "missing data dir: $DATA_DIR"
[[ -d "$SNAPSHOT_DIR" ]] || die "missing snapshot dir: $SNAPSHOT_DIR"
[[ -d "$SNAPSHOT_DATA_DIR" ]] ||
    die "missing snapshot data dir: $SNAPSHOT_DATA_DIR"
[[ -f "$USERS_JSON" ]] || die "missing users.json: $USERS_JSON"

tmpdir="$(mktemp -d)"
trap 'rm -rf "$tmpdir"' EXIT

EMPTY_USERS_JSON="$tmpdir/users.empty.json"
EMPTY_OPAQUE_CREDENTIALS_JSON="$tmpdir/opaque_credentials.empty.json"

log "preparing empty user registry for OPAQUE bootstrap"
printf '%s\n' '{"users":[]}' |
  jq '.' > "$EMPTY_USERS_JSON"

log "preparing empty OPAQUE credentials store"
printf '%s\n' '{"version":1,"accounts":[]}' |
  jq '.' > "$EMPTY_OPAQUE_CREDENTIALS_JSON"

log "stopping $SERVICE_NAME"
systemctl stop "$SERVICE_NAME" || true

log "resetting config json files"
install -o "$PQNAS_USER" -g "$PQNAS_GROUP" -m 0640 \
  "$EMPTY_USERS_JSON" "$USERS_JSON"

printf '%s\n' '{"version":1,"workspaces":[]}' | jq '.' > "$WORKSPACES_JSON"
chown "$PQNAS_USER:$PQNAS_GROUP" "$WORKSPACES_JSON"
chmod 0640 "$WORKSPACES_JSON"

printf '%s\n' '{"shares":[]}' | jq '.' > "$SHARES_JSON"
chown "$PQNAS_USER:$PQNAS_GROUP" "$SHARES_JSON"
chmod 0640 "$SHARES_JSON"

printf '%s\n' '{"devices":{},"refresh_tokens":{},"version":1}' | jq '.' > "$APP_AUTH_JSON"
chown "$PQNAS_USER:$PQNAS_GROUP" "$APP_AUTH_JSON"
chmod 0640 "$APP_AUTH_JSON"

log "resetting per-user password and OPAQUE authentication state"
# Security: remove user-specific credentials and pending enrollment tokens so
# deleted identities cannot authenticate after the development reset. Keep
# opaque_server_setup.bin because it is server-wide cryptographic state.
rm -f \
  "$PASSWORD_CREDENTIALS_JSON" \
  "$OPAQUE_ENROLLMENTS_JSON"

# OPAQUE bootstrap readiness requires a valid credentials store even when it
# contains no accounts. Recreate the empty store instead of deleting it.
install -o "$PQNAS_USER" -g "$PQNAS_GROUP" -m 0600 \
  "$EMPTY_OPAQUE_CREDENTIALS_JSON" "$OPAQUE_CREDENTIALS_JSON"

log "removing old user registry backups"
rm -f "$CONFIG_DIR"/users.json.bak.*

log "resetting share-related state"
rm -rf \
  "$CONFIG_DIR/share_invites_v1" \
  "$CONFIG_DIR/share_manifests_v1" \
  "$CONFIG_DIR/share_recipient_sessions_v1" \
  "$CONFIG_DIR/share_recipients_v1"

mkdir -p \
  "$CONFIG_DIR/share_invites_v1" \
  "$CONFIG_DIR/share_manifests_v1" \
  "$CONFIG_DIR/share_recipient_sessions_v1" \
  "$CONFIG_DIR/share_recipients_v1"

chown -R "$PQNAS_USER:$PQNAS_GROUP" \
  "$CONFIG_DIR/share_invites_v1" \
  "$CONFIG_DIR/share_manifests_v1" \
  "$CONFIG_DIR/share_recipient_sessions_v1" \
  "$CONFIG_DIR/share_recipients_v1"

chmod 0750 \
  "$CONFIG_DIR/share_invites_v1" \
  "$CONFIG_DIR/share_manifests_v1" \
  "$CONFIG_DIR/share_recipient_sessions_v1" \
  "$CONFIG_DIR/share_recipients_v1"

log "removing metadata databases"
for base in file_versions.db gallery_meta.db storage_meta.db trash.db; do
    rm -f \
      "$CONFIG_DIR/$base" \
      "$CONFIG_DIR/$base-shm" \
      "$CONFIG_DIR/$base-wal"
done

log "removing user/workspace/trash/avatar data"
rm -rf \
  "$DATA_DIR/users" \
  "$DATA_DIR/workspaces" \
  "$DATA_DIR/avatars" \
  "$DATA_DIR/.pqnas/trash"

snapshot_real="$(realpath -e "$SNAPSHOT_DIR")"
snapshot_data_real="$(realpath -e "$SNAPSHOT_DATA_DIR")"

[[ "$snapshot_real" == "$SNAPSHOT_DIR" ]] ||
    die "snapshot dir must not be a symlink: $SNAPSHOT_DIR"

[[ "$snapshot_data_real" == "$SNAPSHOT_DATA_DIR" ]] ||
    die "snapshot data dir must not be a symlink: $SNAPSHOT_DATA_DIR"

log "deleting managed Btrfs snapshots"

# Security: readonly Btrfs snapshots must be deleted as subvolumes. The
# root-managed helper validates that every target is directly below the
# PQ-NAS snapshot root before allowing deletion.
while IFS= read -r -d '' target; do
    if "$BTRFS" subvolume show "$target" >/dev/null 2>&1; then
        "$SNAPSHOT_HELPER" delete "$target"
    else
        rm -rf -- "$target"
    fi
done < <(
    find "$SNAPSHOT_DATA_DIR" \
      -mindepth 1 -maxdepth 1 -print0
)

# Remove non-snapshot test files such as .write_test while preserving the
# managed snapshot parent directory itself.
find "$SNAPSHOT_DIR" \
  -mindepth 1 -maxdepth 1 \
  ! -path "$SNAPSHOT_DATA_DIR" \
  -exec rm -rf -- {} +

log "recreating base data directories"
mkdir -p \
  "$DATA_DIR/users" \
  "$DATA_DIR/workspaces" \
  "$DATA_DIR/avatars" \
  "$DATA_DIR/.pqnas/trash/users" \
  "$DATA_DIR/.pqnas/trash/workspaces" \
  "$SNAPSHOT_DATA_DIR"

chown -R "$PQNAS_USER:$PQNAS_GROUP" \
  "$DATA_DIR/users" \
  "$DATA_DIR/workspaces" \
  "$DATA_DIR/avatars" \
  "$DATA_DIR/.pqnas" \
  "$SNAPSHOT_DIR"

# Keep snapshot roots private and preserve group inheritance after reset.
chmod 2750 "$SNAPSHOT_DIR" "$SNAPSHOT_DATA_DIR"

log "starting $SERVICE_NAME"
systemctl start "$SERVICE_NAME"

if ! systemctl is-active --quiet "$SERVICE_NAME"; then
    die "$SERVICE_NAME did not start cleanly"
fi

log "done"
log "all users, user files, workspaces, snapshots, shares, auth tokens, and metadata DBs were reset"
log "next step: sudo pqnas-first-admin cleanup && sudo pqnas-first-admin opaque"
