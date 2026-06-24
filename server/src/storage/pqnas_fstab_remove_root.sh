#!/usr/bin/env bash
# PQ-NAS guarded /etc/fstab mount-entry remover.
#
# Narrow by design:
#   - accepts exactly one argument: /srv/pqnas/pools/<pool_id>
#   - removes only fstab rows whose second field exactly equals that mount path
#   - backs up /etc/fstab before any rewrite
#   - no eval, no broad command execution, no glob-based deletion

set -euo pipefail

die() {
    echo "ERROR: $*" >&2
    exit 1
}

usage() {
    echo "Usage: pqnas-fstab-remove /srv/pqnas/pools/<pool_id>" >&2
    exit 2
}

[[ $# -eq 1 ]] || usage

MOUNT="$1"

case "$MOUNT" in
    /srv/pqnas/pools/*) ;;
    *) die "mount path is not under /srv/pqnas/pools" ;;
esac

POOL_ID="${MOUNT#/srv/pqnas/pools/}"

[[ -n "$POOL_ID" ]] || die "empty pool id"
[[ "$POOL_ID" != */* ]] || die "pool id must be a single path segment"
[[ "$POOL_ID" != "." && "$POOL_ID" != ".." ]] || die "invalid pool id"
[[ "$POOL_ID" =~ ^[A-Za-z0-9][A-Za-z0-9._-]{0,127}$ ]] || die "pool id contains unsafe characters"

case "$MOUNT" in
    *$'\n'*|*$'\r'*|*$'\t'*) die "mount path contains control whitespace" ;;
esac

[[ -f /etc/fstab ]] || die "/etc/fstab is missing"
[[ -r /etc/fstab && -w /etc/fstab ]] || die "/etc/fstab is not readable/writable by root"

TMP=""
COUNT_TMP=""

cleanup() {
    [[ -n "${TMP:-}" ]] && rm -f -- "$TMP"
    [[ -n "${COUNT_TMP:-}" ]] && rm -f -- "$COUNT_TMP"
}
trap cleanup EXIT

TMP="$(mktemp /etc/fstab.pqnas-remove.XXXXXX)"
COUNT_TMP="$(mktemp /tmp/pqnas-fstab-remove-count.XXXXXX)"

awk -v mp="$MOUNT" -v count_file="$COUNT_TMP" '
BEGIN { removed = 0 }
{
    if ($0 ~ /^[[:space:]]*#/ || $0 ~ /^[[:space:]]*$/) {
        print
        next
    }

    if ($2 == mp) {
        removed++
        next
    }

    print
}
END {
    print removed > count_file
}
' /etc/fstab > "$TMP" || die "failed to filter /etc/fstab"

REMOVED="$(cat "$COUNT_TMP" 2>/dev/null || true)"
[[ "$REMOVED" =~ ^[0-9]+$ ]] || die "internal error: bad removed count"

BACKUP="/etc/fstab.pqnas-backup.$(date -u +%Y%m%dT%H%M%SZ)"
if [[ -e "$BACKUP" ]]; then
    BACKUP="${BACKUP}.$$"
fi

cp -a -- /etc/fstab "$BACKUP" || die "failed to back up /etc/fstab"

if (( REMOVED > 0 )); then
    chown --reference=/etc/fstab "$TMP" || die "failed to copy owner"
    chmod --reference=/etc/fstab "$TMP" || die "failed to copy mode"
    mv -- "$TMP" /etc/fstab || die "failed to replace /etc/fstab"
    TMP=""
    echo "ok: removed ${REMOVED} fstab entr$( (( REMOVED == 1 )) && echo y || echo ies ) for ${MOUNT}; backup=${BACKUP}"
else
    echo "ok: no fstab entry found for ${MOUNT}; backup=${BACKUP}"
fi
