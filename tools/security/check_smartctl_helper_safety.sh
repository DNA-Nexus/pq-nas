#!/usr/bin/env bash
set -euo pipefail

HELPER="server/src/storage/pqnas_smartctl_root.sh"
DRIVE="server/src/drive_health.cc"
INSTALLER="tools/installer/pqnas_install.py"

test -f "$HELPER" || {
  echo "ERROR: missing smartctl helper: $HELPER"
  exit 1
}

head -n 1 "$HELPER" | grep -Eq '^#!/(usr/bin|bin)/bash$' || {
  echo "ERROR: smartctl helper must use fixed bash shebang, not /usr/bin/env."
  exit 1
}

rg -q 'unset BASH_ENV ENV CDPATH' "$HELPER" || {
  echo "ERROR: smartctl helper does not clear caller shell environment."
  exit 1
}

rg -q 'export PATH="/usr/sbin:/usr/bin:/sbin:/bin"' "$HELPER" || {
  echo "ERROR: smartctl helper does not set fixed PATH."
  exit 1
}

rg -q 'umask 022' "$HELPER" || {
  echo "ERROR: smartctl helper does not set root-helper umask."
  exit 1
}

rg -q 'pqnas ALL=\(root\) NOPASSWD: /usr/local/sbin/pqnas-smartctl \*' "$INSTALLER" || {
  echo "ERROR: installer smartctl sudoers is not restricted to pqnas-smartctl wrapper."
  exit 1
}

rg -q 'run_command_capture_argv' "$DRIVE" || {
  echo "ERROR: drive_health.cc missing argv capture runner."
  exit 1
}

rg -q 'run_pqnas_smartctl_capture' "$DRIVE" || {
  echo "ERROR: drive_health.cc missing pqnas-smartctl argv helper."
  exit 1
}

if rg -n 'sudo -n .*pqnas-smartctl|std::string\("sudo -n "\) \+ kPqnasSmartctl|kPqnasSmartctl \+ " -' "$DRIVE"; then
  echo
  echo "ERROR: drive_health.cc still builds pqnas-smartctl shell command strings."
  exit 1
fi

if rg -n 'NOPASSWD: .*/usr/sbin/smartctl' tools/installer /etc/sudoers.d 2>/dev/null; then
  echo
  echo "ERROR: direct smartctl sudoers rule found. Use pqnas-smartctl wrapper."
  exit 1
fi

echo "OK: smartctl root helper clears caller environment and uses fixed PATH."
echo "OK: drive_health smartctl calls use argv execution."
echo "OK: smartctl sudoers remains wrapper-scoped."
