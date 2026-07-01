#!/bin/bash
# PQ-NAS guarded drive-bay locate wrapper.
#
# Narrow by design:
#   - accepts only locate-on / locate-off
#   - accepts only real block devices under /dev
#   - rejects partitions; target must be a whole storage device
#   - UI passes only a Linux block device; this wrapper maps it to a physical bay
#   - never accepts a vendor FQDD/slot/command from the UI
#
# Backends:
#   1) Dell iDRAC / RACADM over SSH, only when explicitly enabled in:
#        /etc/pqnas/drive-locate-idrac.env
#   2) ledctl / ledmon generic fallback
#
# Installed as: /usr/local/sbin/pqnas-drive-locate
# Called by pqnas_server through sudoers.

set -euo pipefail
unset BASH_ENV ENV CDPATH
export LC_ALL=C
export PATH="/usr/sbin:/usr/bin:/sbin:/bin"
umask 022

# Security: root helper must not trust caller-controlled PATH or shell env.
AWK="/usr/bin/awk"
CAT="/usr/bin/cat"
CHMOD="/usr/bin/chmod"
CHOWN="/usr/bin/chown"
GREP="/usr/bin/grep"
HEAD="/usr/bin/head"
INSTALL="/usr/bin/install"
LSPCI="/usr/bin/lspci"
LSBLK="/usr/bin/lsblk"
READLINK="/usr/bin/readlink"
SMARTCTL="/usr/sbin/smartctl"
SSH="/usr/bin/ssh"
SSH_KEYGEN="/usr/bin/ssh-keygen"
TIMEOUT="/usr/bin/timeout"
TR="/usr/bin/tr"

D_IDRAC_CFG="/etc/pqnas/drive-locate-idrac.env"
D_IDRAC_DEFAULT_KEY="/etc/pqnas/secrets/pqnas-idrac-locate_rsa"
D_IDRAC_DEFAULT_KNOWN_HOSTS="/etc/pqnas/secrets/pqnas-idrac-known_hosts"

die() {
    echo "ERROR: $*" >&2
    exit 1
}

usage() {
    "${CAT}" >&2 <<'EOF'
Usage:
  pqnas-drive-locate --action locate-on  --device /dev/disk/by-id/...
  pqnas-drive-locate --action locate-off --device /dev/disk/by-id/...

Actions:
  locate-on
  locate-off
EOF
    exit 2
}

trim() {
    local s="${1:-}"
    s="${s#"${s%%[![:space:]]*}"}"
    s="${s%"${s##*[![:space:]]}"}"
    printf '%s' "$s"
}

cfg_value() {
    local key="$1"
    [[ -f "$D_IDRAC_CFG" ]] || return 0

    "${AWK}" -F= -v want="$key" '
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*$/ { next }
        {
            k=$1
            gsub(/^[ \t]+|[ \t]+$/, "", k)
            if (k == want) {
                sub(/^[^=]*=/, "", $0)
                gsub(/^[ \t]+|[ \t]+$/, "", $0)
                gsub(/^"/, "", $0)
                gsub(/"$/, "", $0)
                print $0
                exit
            }
        }
    ' "$D_IDRAC_CFG"
}

truthy() {
    case "$(printf '%s' "${1:-}" | "${TR}" '[:upper:]' '[:lower:]')" in
        1|yes|true|on|enabled) return 0 ;;
        *) return 1 ;;
    esac
}

validate_safe_host() {
    local v="$1"
    [[ -n "$v" ]] || return 1
    [[ "$v" =~ ^[A-Za-z0-9._:-]+$ ]]
}

validate_safe_user() {
    local v="$1"
    [[ -n "$v" ]] || return 1
    [[ "$v" =~ ^[A-Za-z0-9._-]+$ ]]
}

validate_safe_port() {
    local v="$1"
    [[ -n "$v" ]] || return 1
    [[ "$v" =~ ^[0-9]+$ ]] || return 1
    (( v >= 1 && v <= 65535 ))
}

validate_safe_fqdd() {
    local v="$1"
    [[ -n "$v" ]] || return 1
    [[ "$v" =~ ^Disk\.Bay\.[A-Za-z0-9:._-]+$ ]]
}

validate_secret_key_path() {
    local v="$1"
    [[ -n "$v" ]] || return 1
    case "$v" in
        /etc/pqnas/secrets/*) ;;
        *) return 1 ;;
    esac
    [[ -f "$v" ]] || return 1
    [[ -r "$v" ]] || return 1
}

ACTION=""
DEVICE=""
IDRAC_HOST_ARG=""
IDRAC_PORT_ARG=""
IDRAC_USER_ARG=""
IDRAC_ENABLED_ARG=""

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
        --host)
            [[ $# -ge 2 ]] || usage
            IDRAC_HOST_ARG="$2"
            shift 2
            ;;
        --port)
            [[ $# -ge 2 ]] || usage
            IDRAC_PORT_ARG="$2"
            shift 2
            ;;
        --user)
            [[ $# -ge 2 ]] || usage
            IDRAC_USER_ARG="$2"
            shift 2
            ;;
        --enabled)
            [[ $# -ge 2 ]] || usage
            IDRAC_ENABLED_ARG="$2"
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

case "$ACTION" in
    locate-on|locate-off|idrac-status|idrac-save-config|idrac-generate-key|idrac-public-key|idrac-test-connection|idrac-test-inventory) ;;
    *) die "unsupported action: $ACTION" ;;
esac

needs_device=0
case "$ACTION" in
    locate-on|locate-off) needs_device=1 ;;
esac

if [[ "$needs_device" == "1" ]]; then
    case "$DEVICE" in
        /dev/*) ;;
        *) die "device must be under /dev" ;;
    esac
fi

CANON=""
if [[ "$needs_device" == "1" ]]; then
    CANON="$("${READLINK}" -f -- "$DEVICE" 2>/dev/null || true)"
    [[ -n "$CANON" ]] || die "could not resolve device: $DEVICE"

    case "$CANON" in
        /dev/*) ;;
        *) die "resolved device escaped /dev: $CANON" ;;
    esac

    [[ -b "$CANON" ]] || die "not a block device: $CANON"

    TYPE="$("${LSBLK}" -dn -o TYPE -- "$CANON" 2>/dev/null | "${HEAD}" -n1 | "${TR}" -d '[:space:]' || true)"
    [[ "$TYPE" == "disk" ]] || die "target must be a whole storage device, got type=${TYPE:-unknown}: $CANON"
fi

find_ledctl() {
    local p
    for p in /usr/sbin/ledctl /usr/bin/ledctl /sbin/ledctl /bin/ledctl; do
        if [[ -x "$p" ]]; then
            printf '%s' "$p"
            return 0
        fi
    done
    return 1
}

is_megaraid_controller_present() {
    [[ -x "$LSPCI" ]] || return 1
    "$LSPCI" -nn 2>/dev/null | "$GREP" -Eiq 'MegaRAID|PERC|LSI|Broadcom.*SAS|RAID bus controller'
}

device_serial() {
    local s=""
    s="$("${LSBLK}" -dn -o SERIAL -- "$CANON" 2>/dev/null | "${HEAD}" -n1 || true)"
    s="$(trim "$s")"
    if [[ -n "$s" ]]; then
        printf '%s' "$s"
        return 0
    fi

    if [[ -x "$SMARTCTL" ]]; then
        s="$("$SMARTCTL" -i "$CANON" 2>/dev/null | "$AWK" -F: '
            /Serial Number/ {
                v=$2
                gsub(/^[ \t]+|[ \t]+$/, "", v)
                print v
                exit
            }
        ' || true)"
        s="$(trim "$s")"
        if [[ -n "$s" ]]; then
            printf '%s' "$s"
            return 0
        fi
    fi

    return 1
}

run_ledctl_pattern() {
    local pattern="$1"
    local ledctl=""
    local out=""
    local rc=0

    ledctl="$(find_ledctl || true)"
    [[ -n "$ledctl" ]] || die "ledctl not found; install ledmon"

    set +e
    out="$("$ledctl" "${pattern}=${CANON}" 2>&1)"
    rc=$?
    set -e

    if [[ -n "$out" ]]; then
        printf '%s\n' "$out"
    fi

    return "$rc"
}

dell_idrac_enabled() {
    local enabled=""
    enabled="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_ENABLED" || true)"
    truthy "$enabled"
}

dell_idrac_load_config() {
    D_IDRAC_HOST="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_HOST" || true)"
    D_IDRAC_PORT="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_PORT" || true)"
    D_IDRAC_USER="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_USER" || true)"
    D_IDRAC_KEY="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_KEY" || true)"
    D_IDRAC_KNOWN_HOSTS="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_KNOWN_HOSTS" || true)"

    D_IDRAC_PORT="${D_IDRAC_PORT:-22}"
    D_IDRAC_KEY="${D_IDRAC_KEY:-$D_IDRAC_DEFAULT_KEY}"
    D_IDRAC_KNOWN_HOSTS="${D_IDRAC_KNOWN_HOSTS:-$D_IDRAC_DEFAULT_KNOWN_HOSTS}"

    validate_safe_host "$D_IDRAC_HOST" || die "Dell iDRAC backend config has invalid host"
    validate_safe_port "$D_IDRAC_PORT" || die "Dell iDRAC backend config has invalid SSH port"
    validate_safe_user "$D_IDRAC_USER" || die "Dell iDRAC backend config has invalid user"
    validate_secret_key_path "$D_IDRAC_KEY" || die "Dell iDRAC backend key is missing, unreadable, or outside /etc/pqnas/secrets"
    case "$D_IDRAC_KNOWN_HOSTS" in
        /etc/pqnas/secrets/*) ;;
        *) die "Dell iDRAC known_hosts path must be under /etc/pqnas/secrets" ;;
    esac
}

dell_idrac_ssh() {
    local remote_cmd="$1"

    "$TIMEOUT" 45 "$SSH" \
        -i "$D_IDRAC_KEY" \
        -o IdentitiesOnly=yes \
        -o PubkeyAuthentication=yes \
        -o PasswordAuthentication=no \
        -o BatchMode=yes \
        -o ConnectTimeout=10 \
        -o StrictHostKeyChecking=accept-new \
        -o UserKnownHostsFile="$D_IDRAC_KNOWN_HOSTS" \
        -p "$D_IDRAC_PORT" \
        "${D_IDRAC_USER}@${D_IDRAC_HOST}" \
        "$remote_cmd"
}

dell_find_fqdd_for_serial() {
    local serial="$1"
    local inv="$2"

    "${AWK}" -v want="$serial" '
        function trim2(s) {
            gsub(/^[ \t]+|[ \t]+$/, "", s)
            return s
        }

        /^Disk\.Bay\./ {
            cur = trim2($0)
            next
        }

        /^[ \t]*SerialNumber[ \t]*=/ {
            v = $0
            sub(/^.*=/, "", v)
            v = trim2(v)
            if (v == want && cur != "") {
                print cur
                found = 1
                exit
            }
        }

        END {
            if (!found) exit 1
        }
    ' <<<"$inv"
}

run_dell_idrac_locate() {
    local serial=""
    local inv=""
    local fqdd=""
    local cmd=""
    local out=""

    dell_idrac_load_config

    serial="$(device_serial || true)"
    serial="$(trim "$serial")"
    [[ -n "$serial" ]] || die "Dell iDRAC backend could not read serial for $CANON"

    set +e
    inv="$(dell_idrac_ssh "racadm raid get pdisks -o" 2>&1)"
    rc=$?
    set -e
    if [[ $rc -ne 0 ]]; then
        die "Dell iDRAC inventory failed for ${D_IDRAC_HOST}: ${inv}"
    fi

    fqdd="$(dell_find_fqdd_for_serial "$serial" "$inv" || true)"
    fqdd="$(trim "$fqdd")"
    [[ -n "$fqdd" ]] || die "Dell iDRAC backend could not map $CANON serial $serial to a RACADM physical disk FQDD"
    validate_safe_fqdd "$fqdd" || die "Dell iDRAC backend rejected unsafe FQDD for serial $serial: $fqdd"

    if [[ "$ACTION" == "locate-on" ]]; then
        cmd="racadm raid blink:${fqdd}"
    else
        cmd="racadm raid unblink:${fqdd}"
    fi

    set +e
    out="$(dell_idrac_ssh "$cmd" 2>&1)"
    rc=$?
    set -e
    if [[ $rc -ne 0 ]]; then
        die "Dell iDRAC locate command failed for $CANON serial $serial FQDD $fqdd: $out"
    fi

    printf 'Dell iDRAC locate %s OK for %s serial %s FQDD %s\n' "$ACTION" "$CANON" "$serial" "$fqdd"
}

run_ledctl_locate() {
    local out=""
    local out2=""

    if [[ "$ACTION" == "locate-on" ]]; then
        out="$(run_ledctl_pattern locate)" || {
            if is_megaraid_controller_present; then
                die "drive locate is not available through ledctl for ${CANON}. MegaRAID/PERC/LSI controller detected; configure the Dell iDRAC/RACADM backend or install a supported local storcli/perccli backend. ledctl output: ${out}"
            fi
            die "ledctl locate failed for ${CANON}: ${out}"
        }
        exit 0
    fi

    out="$(run_ledctl_pattern normal)" || {
        out2="$(run_ledctl_pattern locate_off)" || {
            if is_megaraid_controller_present; then
                die "drive locate stop is not available through ledctl for ${CANON}. MegaRAID/PERC/LSI controller detected; configure the Dell iDRAC/RACADM backend or install a supported local storcli/perccli backend. ledctl output: ${out}; fallback output: ${out2}"
            fi
            die "ledctl stop locate failed for ${CANON}: ${out}; fallback output: ${out2}"
        }
    }
}


ensure_idrac_secret_dirs() {
    "$INSTALL" -d -m 0755 -o root -g root /etc/pqnas
    "$INSTALL" -d -m 0700 -o root -g root /etc/pqnas/secrets
}

idrac_write_config() {
    local enabled="$IDRAC_ENABLED_ARG"
    local host="$IDRAC_HOST_ARG"
    local port="$IDRAC_PORT_ARG"
    local user="$IDRAC_USER_ARG"

    [[ -n "$enabled" ]] || enabled="0"
    [[ -n "$port" ]] || port="22"

    case "$enabled" in
        1|0|true|false|yes|no|on|off|enabled|disabled) ;;
        *) die "invalid enabled value" ;;
    esac

    validate_safe_host "$host" || die "invalid iDRAC host"
    validate_safe_port "$port" || die "invalid iDRAC SSH port"
    validate_safe_user "$user" || die "invalid iDRAC username"

    ensure_idrac_secret_dirs

    {
        printf 'PQNAS_DRIVE_LOCATE_DELL_IDRAC_ENABLED=%s\n' "$enabled"
        printf 'PQNAS_DRIVE_LOCATE_DELL_IDRAC_HOST=%s\n' "$host"
        printf 'PQNAS_DRIVE_LOCATE_DELL_IDRAC_PORT=%s\n' "$port"
        printf 'PQNAS_DRIVE_LOCATE_DELL_IDRAC_USER=%s\n' "$user"
        printf 'PQNAS_DRIVE_LOCATE_DELL_IDRAC_KEY=%s\n' "$D_IDRAC_DEFAULT_KEY"
        printf 'PQNAS_DRIVE_LOCATE_DELL_IDRAC_KNOWN_HOSTS=%s\n' "$D_IDRAC_DEFAULT_KNOWN_HOSTS"
    } > "$D_IDRAC_CFG"

    "$CHMOD" 0600 "$D_IDRAC_CFG"
    "$CHOWN" root:root "$D_IDRAC_CFG"

    printf 'saved=1\n'
}

idrac_generate_key() {
    ensure_idrac_secret_dirs

    if [[ ! -f "$D_IDRAC_DEFAULT_KEY" ]]; then
        [[ -x "$SSH_KEYGEN" ]] || die "ssh-keygen not found"
        "$SSH_KEYGEN" -t rsa -b 4096 -f "$D_IDRAC_DEFAULT_KEY" -N "" -C "pqnas-drive-locate" >/dev/null
        "$CHMOD" 0600 "$D_IDRAC_DEFAULT_KEY"
        "$CHMOD" 0644 "${D_IDRAC_DEFAULT_KEY}.pub"
        "$CHOWN" root:root "$D_IDRAC_DEFAULT_KEY" "${D_IDRAC_DEFAULT_KEY}.pub"
    fi

    [[ -r "${D_IDRAC_DEFAULT_KEY}.pub" ]] || die "public key was not created"
    printf 'generated=1\n'
    printf 'public_key='
    "${CAT}" "${D_IDRAC_DEFAULT_KEY}.pub"
}

idrac_public_key() {
    [[ -r "${D_IDRAC_DEFAULT_KEY}.pub" ]] || die "public key not found; generate key first"
    "${CAT}" "${D_IDRAC_DEFAULT_KEY}.pub"
}

idrac_status() {
    local enabled host port user key pub kh

    enabled="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_ENABLED" || true)"
    host="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_HOST" || true)"
    port="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_PORT" || true)"
    user="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_USER" || true)"
    key="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_KEY" || true)"
    kh="$(cfg_value "PQNAS_DRIVE_LOCATE_DELL_IDRAC_KNOWN_HOSTS" || true)"

    [[ -n "$port" ]] || port="22"
    [[ -n "$key" ]] || key="$D_IDRAC_DEFAULT_KEY"
    [[ -n "$kh" ]] || kh="$D_IDRAC_DEFAULT_KNOWN_HOSTS"

    pub="${key}.pub"

    printf 'enabled=%s\n' "$enabled"
    printf 'host=%s\n' "$host"
    printf 'port=%s\n' "$port"
    printf 'user=%s\n' "$user"
    printf 'key_exists=%s\n' "$([[ -f "$key" ]] && echo 1 || echo 0)"
    printf 'public_key_exists=%s\n' "$([[ -f "$pub" ]] && echo 1 || echo 0)"
    printf 'known_hosts_exists=%s\n' "$([[ -f "$kh" ]] && echo 1 || echo 0)"
}

idrac_test_connection() {
    dell_idrac_load_config

    local out=""
    local rc=0
    set +e
    out="$(dell_idrac_ssh "racadm getversion" 2>&1)"
    rc=$?
    set -e

    if [[ $rc -ne 0 ]]; then
        die "Dell iDRAC SSH/RACADM connection test failed: $out"
    fi

    printf 'connection=ok\n'
    printf '%s\n' "$out"
}

idrac_test_inventory() {
    dell_idrac_load_config

    local inv=""
    local rc=0
    set +e
    inv="$(dell_idrac_ssh "racadm raid get pdisks -o" 2>&1)"
    rc=$?
    set -e

    if [[ $rc -ne 0 ]]; then
        die "Dell iDRAC RACADM inventory failed: $inv"
    fi

    printf 'inventory=ok\n'
    "${AWK}" '
        /^Disk\.Bay\./ {
            fqdd=$0
            next
        }
        /^[ \t]*Name[ \t]*=/ {
            name=$0
            sub(/^.*=/, "", name)
            gsub(/^[ \t]+|[ \t]+$/, "", name)
            next
        }
        /^[ \t]*SerialNumber[ \t]*=/ {
            serial=$0
            sub(/^.*=/, "", serial)
            gsub(/^[ \t]+|[ \t]+$/, "", serial)
            if (fqdd != "") {
                printf "%s | %s | %s\n", serial, name, fqdd
            }
        }
    ' <<<"$inv"
}

case "$ACTION" in
    idrac-status)
        idrac_status
        exit 0
        ;;
    idrac-save-config)
        idrac_write_config
        exit 0
        ;;
    idrac-generate-key)
        idrac_generate_key
        exit 0
        ;;
    idrac-public-key)
        idrac_public_key
        exit 0
        ;;
    idrac-test-connection)
        idrac_test_connection
        exit 0
        ;;
    idrac-test-inventory)
        idrac_test_inventory
        exit 0
        ;;
esac

if dell_idrac_enabled; then
    run_dell_idrac_locate
    exit 0
fi

run_ledctl_locate
exit 0
