#!/usr/bin/env bash
set -euo pipefail

# Security guard: storage/RAID code must not regain direct sudo access to broad
# root tools. Privileged storage operations must go through guarded helpers.

PATTERN='sudo -n /(usr/sbin|usr/bin|bin)/(mkfs\.btrfs|wipefs|sgdisk|partprobe|udevadm|mount|umount|mkdir|chown|chmod|btrfs)'

TARGETS=(
  server/src/routes/routes_storage_raid.cpp
  server/src/main.cpp
)

if rg -n "$PATTERN" "${TARGETS[@]}"; then
  echo
  echo "ERROR: direct dangerous storage sudo call found."
  echo "Route privileged storage operations through pqnas-raid-root or another guarded helper."
  exit 1
fi

LEGACY_SUDOERS_PATTERN='install_sudoers_rule\("pqnas-raid"|Cmnd_Alias PQNAS_BTRFS|Cmnd_Alias PQNAS_PART|NOPASSWD: .*/usr/sbin/sgdisk \*|NOPASSWD: .*/usr/sbin/wipefs \*|NOPASSWD: .*/usr/bin/btrfs device add \*'

LEGACY_TARGETS=(
  tools/installer
  tools/release
  server/src
)

# Security guard: fresh installs must not recreate the old broad pqnas-raid sudoers file.
if rg -n "$LEGACY_SUDOERS_PATTERN" "${LEGACY_TARGETS[@]}"; then
  echo
  echo "ERROR: legacy broad pqnas-raid sudoers pattern found."
  echo "Use pqnas-raid-root with validated helper actions instead."
  exit 1
fi

echo "OK: no direct dangerous storage sudo calls found."
echo "OK: no legacy broad pqnas-raid sudoers pattern found."
