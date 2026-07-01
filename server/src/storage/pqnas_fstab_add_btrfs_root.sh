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
    echo "Usage: pqnas-fstab-add-btrfs /srv/pqnas/pools/<pool_id>" >&2
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

[ -d "$mount" ] || die "mount directory does not exist: $mount"
# hardening: reject symlink mountpoints to reduce symlink/TOCTOU risk
[ ! -L "$mount" ] || die "mount path is a symlink: $mount"

# hardening: require the exact path to be an active mountpoint
target="$("$FINDMNT" -rn --target "$mount" -o TARGET 2>/dev/null | "$HEAD" -n1 || true)"
[ "$target" = "$mount" ] || die "path is not an exact mountpoint: $mount"

# hardening: fail closed unless the target is a mounted btrfs filesystem
fstype="$("$FINDMNT" -rn --target "$mount" -o FSTYPE 2>/dev/null | "$HEAD" -n1 || true)"
[ "$fstype" = "btrfs" ] || die "mount is not btrfs: $mount"

uuid="$("$FINDMNT" -rn --target "$mount" -o UUID 2>/dev/null | "$HEAD" -n1 | "$TR" -d '[:space:]' || true)"

if [ -z "$uuid" ]; then
    src="$("$FINDMNT" -rn --target "$mount" -o SOURCE 2>/dev/null | "$HEAD" -n1 | "$TR" -d '[:space:]' || true)"
    if [ -n "$src" ]; then
        uuid="$("$BLKID" -s UUID -o value "$src" 2>/dev/null | "$HEAD" -n1 | "$TR" -d '[:space:]' || true)"
    fi
fi

[ -n "$uuid" ] || die "could not resolve UUID for mount: $mount"

# hardening: UUID allowlist prevents unsafe data entering /etc/fstab
case "$uuid" in
    *[!A-Za-z0-9-]*)
        die "unsafe UUID"
        ;;
esac

# hardening: create temp file under /etc, not global /tmp
tmp="$("$MKTEMP" /etc/fstab.pqnas-add.XXXXXX)"
cleanup_tmp() {
    [ -n "${tmp:-}" ] && "$RM" -f "$tmp"
}
trap cleanup_tmp EXIT INT TERM

"$AWK" -v m="$mount" '$2 != m { print }' "$FSTAB" > "$tmp"
printf 'UUID=%s %s btrfs defaults,nofail,x-systemd.device-timeout=10 0 0\n' "$uuid" "$mount" >> "$tmp"

# hardening: atomic-ish root-owned replacement avoids partial fstab writes
"$INSTALL" -m 0644 -o root -g root "$tmp" "$FSTAB"
"$RM" -f "$tmp"
trap - EXIT INT TERM

echo "ok: fstab updated mount=$mount uuid=$uuid"
