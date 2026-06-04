#!/usr/bin/env bash
# PQ-NAS guarded drive-bay locate wrapper.
#
# Narrow by design:
#   - accepts only locate-on / locate-off
#   - accepts only real block devices under /dev
#   - rejects partitions; target must be a whole storage device
#   - does not assume HDD; SSD/NVMe are valid if the bay/enclosure supports locate
#   - currently uses ledctl only; SES/sg_ses mapping can be added later
#
# Installed as: /usr/local/sbin/pqnas-drive-locate
# Called by pqnas_server through sudoers.

set -euo pipefail

die() {
    echo "ERROR: $*" >&2
    exit 1
}

usage() {
    cat >&2 <<'EOF'
Usage:
  pqnas-drive-locate --action locate-on  --device /dev/disk/by-id/...
  pqnas-drive-locate --action locate-off --device /dev/disk/by-id/...

Actions:
  locate-on
  locate-off
EOF
    exit 2
}

ACTION=""
DEVICE=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --action)
            [[ $# -ge 2 ]] || usage
            ACTION="$2"
            shift 2
            ;;
        --device)
            [[ $# -ge 2 ]] || usage
            DEVICE="$2"
            shift 2
            ;;
        -h|--help)
            usage
            ;;
        *)
            die "unknown argument: $1"
            ;;
    esac
done

[[ -n "$ACTION" ]] || usage
[[ -n "$DEVICE" ]] || usage

case "$ACTION" in
    locate-on|locate-off)
        ;;
    *)
        die "unsupported action: $ACTION"
        ;;
esac

case "$DEVICE" in
    /dev/*)
        ;;
    *)
        die "device must be under /dev"
        ;;
esac

CANON="$(readlink -f -- "$DEVICE" 2>/dev/null || true)"
[[ -n "$CANON" ]] || die "could not resolve device: $DEVICE"

case "$CANON" in
    /dev/*)
        ;;
    *)
        die "resolved device escaped /dev: $CANON"
        ;;
esac

[[ -b "$CANON" ]] || die "not a block device: $CANON"

TYPE="$(lsblk -dn -o TYPE -- "$CANON" 2>/dev/null | head -n1 | tr -d '[:space:]' || true)"
[[ "$TYPE" == "disk" ]] || die "target must be a whole storage device, got type=${TYPE:-unknown}: $CANON"

LEDCTL=""
for p in /usr/sbin/ledctl /usr/bin/ledctl /sbin/ledctl /bin/ledctl; do
    if [[ -x "$p" ]]; then
        LEDCTL="$p"
        break
    fi
done

[[ -n "$LEDCTL" ]] || die "ledctl not found; install ledmon"

if [[ "$ACTION" == "locate-on" ]]; then
    exec "$LEDCTL" "locate=$CANON"
else
    exec "$LEDCTL" "locate_off=$CANON"
fi
