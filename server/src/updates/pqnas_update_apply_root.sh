#!/bin/sh
set -eu

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

exec /usr/bin/python3 /usr/local/libexec/pqnas/pqnas_update_apply.py \
    --plan-id "$PLAN_ID" \
    --apply
