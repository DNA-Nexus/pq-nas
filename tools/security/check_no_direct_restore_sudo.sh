#!/usr/bin/env bash
set -euo pipefail

# Security guard: snapshot/restore code must not regain direct sudo access to
# broad btrfs/systemctl tools. Use pqnas-restore-root validated actions instead.

DIRECT_PATTERN='sudo -n /usr/bin/(btrfs subvolume|systemctl)|std::string\("sudo -n "\) \+ BTRFS|BTRFS \+ " subvolume show'

TARGETS=(
  server/src/routes/routes_snapshots_create.cpp
  server/src/routes/routes_snapshots_restore.cpp
  server/src/routes/routes_files_put.inc
  server/src/main.cpp
)

if rg -n "$DIRECT_PATTERN" "${TARGETS[@]}"; then
  echo
  echo "ERROR: direct restore/snapshot sudo call found."
  echo "Route privileged restore/snapshot operations through pqnas-restore-root."
  exit 1
fi

LEGACY_SUDOERS_PATTERN='NOPASSWD: /usr/bin/btrfs subvolume show \*|NOPASSWD: /usr/bin/systemctl start pqnas-restore@\*\.service|NOPASSWD: /usr/bin/systemctl show pqnas-restore@\*\.service'

if rg -n "$LEGACY_SUDOERS_PATTERN" tools/installer server/src tools/release; then
  echo
  echo "ERROR: legacy broad pqnas-restore sudoers pattern found."
  echo "Use pqnas-restore-root with validated helper actions instead."
  exit 1
fi

echo "OK: no direct restore/snapshot sudo calls found."
echo "OK: no legacy broad pqnas-restore sudoers pattern found."
