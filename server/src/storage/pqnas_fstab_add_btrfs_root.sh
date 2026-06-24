#!/bin/sh
set -eu

mount="${1:-}"

case "$mount" in
  /srv/pqnas/pools/*) ;;
  *)
    echo "error: mount must be under /srv/pqnas/pools" >&2
    exit 2
    ;;
esac

if [ ! -d "$mount" ]; then
  echo "error: mount directory does not exist: $mount" >&2
  exit 2
fi

uuid="$(findmnt -no UUID --target "$mount" 2>/dev/null | head -n1 | tr -d '[:space:]' || true)"

if [ -z "$uuid" ]; then
  src="$(findmnt -no SOURCE --target "$mount" 2>/dev/null | head -n1 | tr -d '[:space:]' || true)"
  if [ -n "$src" ]; then
    uuid="$(blkid -s UUID -o value "$src" 2>/dev/null | head -n1 | tr -d '[:space:]' || true)"
  fi
fi

if [ -z "$uuid" ]; then
  echo "error: could not resolve UUID for mount: $mount" >&2
  exit 1
fi

case "$uuid" in
  *[!A-Za-z0-9-]*)
    echo "error: unsafe UUID: $uuid" >&2
    exit 1
    ;;
esac

tmp="$(mktemp /etc/fstab.pqnas.XXXXXX)"
awk -v m="$mount" '$2 != m { print }' /etc/fstab > "$tmp"
printf 'UUID=%s %s btrfs defaults,nofail,x-systemd.device-timeout=10 0 0\n' "$uuid" "$mount" >> "$tmp"
install -m 0644 -o root -g root "$tmp" /etc/fstab
rm -f "$tmp"

echo "ok: fstab updated mount=$mount uuid=$uuid"
