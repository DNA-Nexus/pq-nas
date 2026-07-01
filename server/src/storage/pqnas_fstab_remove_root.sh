#!/bin/sh
set -eu
unset BASH_ENV ENV CDPATH
export LC_ALL=C
export PATH="/usr/sbin:/usr/bin:/sbin:/bin"
umask 022

# Security: root helper must not trust caller-controlled PATH or shell env.
FINDMNT="/usr/bin/findmnt"
HEAD="/usr/bin/head"
TR="/usr/bin/tr"
BLKID="/usr/sbin/blkid"
AWK="/usr/bin/awk"
MKTEMP="/usr/bin/mktemp"
INSTALL="/usr/bin/install"
RM="/usr/bin/rm"
FSTAB="/etc/fstab"

if [ ! -x "$BLKID" ] && [ -x /usr/bin/blkid ]; then
    BLKID="/usr/bin/blkid"
fi

die() {
    echo "error: $*" >&2
    exit 1
}

usage() {
    echo "Usage: pqnas-fstab-remove /srv/pqnas/pools/<pool_id>" >&2
    exit 2
}

[ "$#" -eq 1 ] || usage

mount="$1"

# hardening: allow only PQ-NAS managed pool mount paths
case "$mount" in
    /srv/pqnas/pools/*) ;;
    *) die "mount must be under /srv/pqnas/pools" ;;
esac

pool_id="${mount#/srv/pqnas/pools/}"

# hardening: strict pool id allowlist prevents path traversal and metacharacters
case "$pool_id" in
    ""|*/*|*[!A-Za-z0-9_-]*)
        die "unsafe pool id"
        ;;
esac

[ "${#pool_id}" -le 32 ] || die "pool id too long"
# hardening: canonical mount string prevents alternate path spellings
[ "$mount" = "/srv/pqnas/pools/$pool_id" ] || die "non-canonical mount path"

parent="/srv/pqnas/pools"
[ -d "$parent" ] || die "pool parent does not exist: $parent"

parent_real="$(cd "$parent" && pwd -P)"
[ "$parent_real" = "$parent" ] || die "pool parent is not canonical: $parent_real"

# hardening: reject symlink mountpoints to reduce symlink/TOCTOU risk
if [ -e "$mount" ] && [ -L "$mount" ]; then
    die "mount path is a symlink: $mount"
fi

# hardening: count matching fstab rows without using a /tmp side file
removed_count="$("$AWK" -v m="$mount" '$2 == m { c++ } END { print c + 0 }' "$FSTAB")"

# hardening: create temp file under /etc, not global /tmp
tmp="$("$MKTEMP" /etc/fstab.pqnas-remove.XXXXXX)"
cleanup_tmp() {
    [ -n "${tmp:-}" ] && "$RM" -f "$tmp"
}
trap cleanup_tmp EXIT INT TERM

"$AWK" -v m="$mount" '$2 != m { print }' "$FSTAB" > "$tmp"

# hardening: atomic-ish root-owned replacement avoids partial fstab writes
"$INSTALL" -m 0644 -o root -g root "$tmp" "$FSTAB"
"$RM" -f "$tmp"
trap - EXIT INT TERM

echo "ok: fstab updated mount=$mount removed=$removed_count"
