#!/bin/sh
set -eu
export LC_ALL=C

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
removed_count="$(awk -v m="$mount" '$2 == m { c++ } END { print c + 0 }' /etc/fstab)"

# hardening: create temp file under /etc, not global /tmp
tmp="$(mktemp /etc/fstab.pqnas-remove.XXXXXX)"
trap 'rm -f "$tmp"' EXIT INT TERM

awk -v m="$mount" '$2 != m { print }' /etc/fstab > "$tmp"

# hardening: atomic-ish root-owned replacement avoids partial fstab writes
install -m 0644 -o root -g root "$tmp" /etc/fstab
rm -f "$tmp"
trap - EXIT INT TERM

echo "ok: fstab updated mount=$mount removed=$removed_count"
