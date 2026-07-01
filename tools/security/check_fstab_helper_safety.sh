#!/usr/bin/env bash
set -euo pipefail

HELPERS=(
  server/src/storage/pqnas_fstab_add_btrfs_root.sh
  server/src/storage/pqnas_fstab_remove_root.sh
)

for helper in "${HELPERS[@]}"; do
  test -f "$helper" || {
    echo "ERROR: missing helper: $helper"
    exit 1
  }

  rg -q 'unset BASH_ENV ENV CDPATH' "$helper" || {
    echo "ERROR: helper does not clear shell environment: $helper"
    exit 1
  }

  rg -q 'export PATH="/usr/sbin:/usr/bin:/sbin:/bin"' "$helper" || {
    echo "ERROR: helper does not set fixed PATH: $helper"
    exit 1
  }

  rg -q 'pool_id="\$\{mount#/srv/pqnas/pools/\}"' "$helper" || {
    echo "ERROR: helper does not derive pool_id from managed pool root: $helper"
    exit 1
  }

  rg -q '\[ "\$mount" = "/srv/pqnas/pools/\$pool_id" \]' "$helper" || {
    echo "ERROR: helper does not enforce canonical mount string: $helper"
    exit 1
  }
done

rg -q 'pqnas ALL=\(root\) NOPASSWD: /usr/local/sbin/pqnas-fstab-add-btrfs /srv/pqnas/pools/\*' tools/installer/pqnas_install.py || {
  echo "ERROR: installer fstab-add sudoers is not helper/path-scoped."
  exit 1
}

rg -q 'pqnas ALL=\(root\) NOPASSWD: /usr/local/sbin/pqnas-fstab-remove /srv/pqnas/pools/\*' tools/installer/pqnas_install.py || {
  echo "ERROR: installer fstab-remove sudoers is not helper/path-scoped."
  exit 1
}

BAD_SUDOERS_PATTERN='NOPASSWD: .*(/etc/fstab|/usr/bin/(mount|umount|tee|awk|sed|install|sh|bash)|/bin/(mount|umount|sh|bash)|/usr/sbin/blkid)'

if rg -n "$BAD_SUDOERS_PATTERN" tools/installer server/src tools/release; then
  echo
  echo "ERROR: direct fstab/mount-related sudoers rule found."
  echo "Use pqnas-fstab-add-btrfs or pqnas-fstab-remove guarded helpers instead."
  exit 1
fi

echo "OK: fstab helpers clear caller environment and use fixed PATH."
echo "OK: fstab sudoers remains helper/path-scoped."
echo "OK: no direct fstab/mount sudoers rules found."
