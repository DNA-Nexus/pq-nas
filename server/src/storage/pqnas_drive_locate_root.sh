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

is_megaraid_controller_present() {
    command -v lspci >/dev/null 2>&1 || return 1
    lspci -nn 2>/dev/null | grep -Eiq 'MegaRAID|PERC|LSI|Broadcom.*SAS|RAID bus controller'
}

run_ledctl_pattern() {
    local pattern="$1"
    local out=""
    local rc=0

    set +e
    out="$("$LEDCTL" "${pattern}=${CANON}" 2>&1)"
    rc=$?
    set -e

    if [[ -n "$out" ]]; then
        printf '%s\n' "$out"
    fi

    return "$rc"
}

if [[ "$ACTION" == "locate-on" ]]; then
    OUT="$(run_ledctl_pattern locate)" || {
        RC=$?
        if is_megaraid_controller_present; then
            die "drive locate is not available through ledctl for ${CANON}. MegaRAID/PERC/LSI controller detected; this hardware usually needs a storcli/perccli backend or iDRAC/OpenManage locate support. ledctl output: ${OUT}"
        fi
        die "ledctl locate failed for ${CANON}: ${OUT}"
    }
    exit 0
fi

# Stop locate. ledctl commonly supports normal=...; keep locate_off as fallback.
OUT="$(run_ledctl_pattern normal)" || {
    RC=$?
    OUT2="$(run_ledctl_pattern locate_off)" || {
        RC2=$?
        if is_megaraid_controller_present; then
            die "drive locate stop is not available through ledctl for ${CANON}. MegaRAID/PERC/LSI controller detected; this hardware usually needs a storcli/perccli backend or iDRAC/OpenManage locate support. ledctl output: ${OUT}; fallback output: ${OUT2}"
        fi
        die "ledctl stop locate failed for ${CANON}: ${OUT}; fallback output: ${OUT2}"
    }
}
exit 0
