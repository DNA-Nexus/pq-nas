#!/usr/bin/env bash
set -euo pipefail

WRAPPER="server/src/updates/pqnas_update_apply_root.sh"
ROUTES="server/src/updates/update_center_routes.cpp"
INSTALLER="tools/installer/pqnas_install.py"

test -f "$WRAPPER" || {
  echo "ERROR: missing update apply root wrapper: $WRAPPER"
  exit 1
}

rg -q 'unset BASH_ENV ENV CDPATH' "$WRAPPER" || {
  echo "ERROR: update apply root wrapper does not clear shell environment."
  exit 1
}

rg -q 'export PATH="/usr/sbin:/usr/bin:/sbin:/bin"' "$WRAPPER" || {
  echo "ERROR: update apply root wrapper does not set fixed PATH."
  exit 1
}

rg -q 'exec "\$ENV_BIN" -i' "$WRAPPER" || {
  echo "ERROR: update apply root wrapper does not run Python helper through clean env."
  exit 1
}

rg -q 'PYTHONNOUSERSITE=1' "$WRAPPER" || {
  echo "ERROR: update apply root wrapper does not disable user site packages."
  exit 1
}

rg -q 'pqnas ALL=\(root\) NOPASSWD: /usr/local/sbin/pqnas-update-apply --plan-id \*' "$INSTALLER" || {
  echo "ERROR: installer update-apply sudoers is not restricted to --plan-id."
  exit 1
}

BAD_ROUTE_PATTERN='timeout 120 sudo -n|PQNAS_UPDATE_APPLY_HELPER_PATH|update_shell_quote\(apply_helper_path\)|sudo -n .*/usr/local/sbin/pqnas-update-apply'

if rg -n "$BAD_ROUTE_PATTERN" "$ROUTES"; then
  echo
  echo "ERROR: update apply route uses shell/env-selected root helper execution."
  echo "Use fixed /usr/bin/sudo argv execution for pqnas-update-apply."
  exit 1
fi

rg -q '"/usr/local/sbin/pqnas-update-apply"' "$ROUTES" || {
  echo "ERROR: update apply route does not reference fixed root helper path."
  exit 1
}

echo "OK: update apply root wrapper clears caller environment."
echo "OK: update apply route uses fixed helper path without shell/env-selected root helper execution."
echo "OK: update-apply sudoers remains --plan-id scoped."
