#!/usr/bin/env bash
set -euo pipefail

RAID_HELPER="server/src/storage/pqnas_raid_root.sh"
BTRFS_STATUS_HELPER="server/src/storage/pqnas_btrfs_status_root.sh"
INSTALLER="tools/installer/pqnas_install.py"

for helper in "$RAID_HELPER" "$BTRFS_STATUS_HELPER"; do
  test -f "$helper" || {
    echo "ERROR: missing helper: $helper"
    exit 1
  }

  head -n 1 "$helper" | grep -Eq '^#!/(usr/bin|bin)/bash$' || {
    echo "ERROR: helper must use fixed bash shebang, not /usr/bin/env: $helper"
    exit 1
  }

  rg -q 'unset BASH_ENV ENV CDPATH' "$helper" || {
    echo "ERROR: helper does not clear caller shell environment: $helper"
    exit 1
  }

  rg -q 'export PATH="/usr/sbin:/usr/bin:/sbin:/bin"' "$helper" || {
    echo "ERROR: helper does not set fixed PATH: $helper"
    exit 1
  }

  rg -q 'umask 022' "$helper" || {
    echo "ERROR: helper does not set root-helper umask: $helper"
    exit 1
  }
done

rg -q 'pqnas ALL=\(root\) NOPASSWD: /usr/local/sbin/pqnas-raid-root \*' "$INSTALLER" || {
  echo "ERROR: installer raid sudoers is not restricted to pqnas-raid-root helper."
  exit 1
}

rg -q 'pqnas ALL=\(root\) NOPASSWD: /usr/local/sbin/pqnas-btrfs-status \*' "$INSTALLER" || {
  echo "ERROR: installer btrfs-status sudoers is not restricted to pqnas-btrfs-status helper."
  exit 1
}

BAD_SUDOERS_PATTERN='NOPASSWD: .*(/usr/bin/btrfs|/bin/mount|/bin/umount|/usr/sbin/mkfs\.btrfs|/usr/sbin/wipefs|/usr/sbin/sgdisk|/usr/sbin/partprobe|/usr/sbin/mdadm)'

if rg -n "$BAD_SUDOERS_PATTERN" tools/installer /etc/sudoers.d 2>/dev/null; then
  echo
  echo "ERROR: direct storage sudoers rule found. Use pqnas-raid-root or pqnas-btrfs-status helpers."
  exit 1
fi

echo "OK: RAID and Btrfs status helpers clear caller environment and use fixed PATH."
echo "OK: storage sudoers remains helper-scoped."
echo "NOTE: C++ RAID command strings are intentionally not blocked by this guard yet."
echo "      They need a separate plan/executor argv migration PR."
