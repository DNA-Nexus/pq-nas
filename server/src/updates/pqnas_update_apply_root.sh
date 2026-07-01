#!/bin/sh
set -eu
unset BASH_ENV ENV CDPATH
export LC_ALL=C
export PATH="/usr/sbin:/usr/bin:/sbin:/bin"
umask 022

# Security: root wrapper must not trust caller-controlled PATH or shell env.
PYTHON3="/usr/bin/python3"
ENV_BIN="/usr/bin/env"
APPLY_HELPER="/usr/local/libexec/pqnas/pqnas_update_apply.py"

json_fail() {
    code="$1"
    msg="$2"
    printf '{\n'
    printf '  "ok": false,\n'
    printf '  "error": "%s",\n' "$code"
    printf '  "message": "%s",\n' "$msg"
    printf '  "install_performed": false\n'
    printf '}\n'
    exit 1
}

if [ "$#" -ne 2 ] || [ "$1" != "--plan-id" ]; then
    json_fail "bad_args" "Expected: --plan-id PLAN_ID"
fi

PLAN_ID="$2"

case "$PLAN_ID" in
    ""|*/*|*\\*|*..*)
        json_fail "bad_plan_id" "Invalid plan_id path"
        ;;
esac

case "$PLAN_ID" in
    *[!A-Za-z0-9._-]*)
        json_fail "bad_plan_id_chars" "Invalid plan_id characters"
        ;;
esac

[ "${#PLAN_ID}" -le 240 ] || json_fail "bad_plan_id" "Invalid plan_id length"
[ -x "$PYTHON3" ] || json_fail "missing_python" "python3 not found"
[ -f "$APPLY_HELPER" ] || json_fail "missing_helper" "Update apply helper not found"

# Security: drop caller env before running the Python root helper. The helper
# must use fixed root-controlled defaults, not PQNAS_* overrides from sudo env.
exec "$ENV_BIN" -i \
    LC_ALL=C \
    PATH="/usr/sbin:/usr/bin:/sbin:/bin" \
    PYTHONNOUSERSITE=1 \
    "$PYTHON3" "$APPLY_HELPER" \
    --plan-id "$PLAN_ID" \
    --apply
