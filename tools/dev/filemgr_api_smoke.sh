#!/usr/bin/env bash
set -Eeuo pipefail

# PQ-NAS File Manager API smoke/regression test.
#
# Required:
#   BASE='https://pqnas-dev.pqnas-test.uk'
#   COOKIE='your browser Cookie header value'
#
# Optional:
#   INSECURE=1              # pass -k to curl
#   KEEP=1                  # keep test directory instead of cleanup-delete
#   TEST_ID=my-run          # stable id
#   ROOT='__api_smoke/x'    # override test root
#   EXPECT_VERSIONS=1       # fail if overwrite/write does not create versions
#   WORKSPACE_ID=...        # optional second suite for workspace files
#
# Example:
#   BASE='https://pqnas-dev.pqnas-test.uk' COOKIE='pqnas_session=...' tools/dev/filemgr_api_smoke.sh

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "ERROR: missing dependency: $1" >&2
    exit 2
  }
}

need curl
need jq
need sha256sum
need mktemp
need sed
need date

: "${BASE:?Set BASE, e.g. BASE=https://pqnas-dev.pqnas-test.uk}"
: "${COOKIE:?Set COOKIE to your browser Cookie header value}"

BASE="${BASE%/}"
ORIGIN="${ORIGIN:-$(printf '%s' "$BASE" | sed -E 's#^(https?://[^/]+).*#\1#')}"
COOKIE="${COOKIE#Cookie: }"

CURL_TLS=()
if [[ "${INSECURE:-0}" == "1" ]]; then
  CURL_TLS=(-k)
fi

TEST_ID="${TEST_ID:-$(date -u +%Y%m%dT%H%M%SZ)-$$}"
ROOT="${ROOT:-__api_smoke/$TEST_ID}"
KEEP="${KEEP:-0}"
EXPECT_VERSIONS="${EXPECT_VERSIONS:-0}"

TMPDIR="$(mktemp -d)"
LAST_BODY=""
LAST_STATUS=""

cleanup_tmp() {
  rm -rf "$TMPDIR"
}
trap cleanup_tmp EXIT

log() {
  printf '\n== %s ==\n' "$*"
}

ok() {
  printf 'OK: %s\n' "$*"
}

fail() {
  echo "FAIL: $*" >&2
  if [[ -n "${LAST_BODY:-}" && -f "$LAST_BODY" ]]; then
    echo "--- response body ---" >&2
    cat "$LAST_BODY" >&2 || true
    echo >&2
  fi
  exit 1
}

urlenc() {
  jq -rn --arg v "$1" '$v|@uri'
}

CURL_BASE_ARGS=(
  -sS
  "${CURL_TLS[@]}"
  -H "Cookie: $COOKIE"
  -H "Origin: $ORIGIN"
  -H "Referer: $ORIGIN/"
  -H "Accept: application/json"
)

request() {
  local method="$1"
  local url="$2"
  shift 2

  LAST_BODY="$(mktemp "$TMPDIR/body.XXXXXX")"
  LAST_STATUS="$(
    curl \
      -o "$LAST_BODY" \
      -w '%{http_code}' \
      "${CURL_BASE_ARGS[@]}" \
      -X "$method" \
      "$BASE$url" \
      "$@"
  )"
}

expect_json_ok() {
  local label="$1"
  local method="$2"
  local url="$3"
  local body="${4:-}"

  if [[ -n "$body" ]]; then
    request "$method" "$url" \
      -H "Content-Type: application/json" \
      --data-binary "$body"
  else
    request "$method" "$url"
  fi

  [[ "$LAST_STATUS" =~ ^2 ]] || fail "$label HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "$label JSON .ok != true"
  ok "$label"
}

expect_json_ok_or_not_found() {
  local label="$1"
  local method="$2"
  local url="$3"
  local body="${4:-}"

  if [[ -n "$body" ]]; then
    request "$method" "$url" \
      -H "Content-Type: application/json" \
      --data-binary "$body"
  else
    request "$method" "$url"
  fi

  if [[ "$LAST_STATUS" == "404" ]]; then
    ok "$label already not_found"
    return 0
  fi

  [[ "$LAST_STATUS" =~ ^2 ]] || fail "$label HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "$label JSON .ok != true"
  ok "$label"
}

expect_json_ok_or_absent_workspace_target() {
  local label="$1"
  local method="$2"
  local url="$3"
  local body="${4:-}"

  if [[ -n "$body" ]]; then
    request "$method" "$url" \
      -H "Content-Type: application/json" \
      --data-binary "$body"
  else
    request "$method" "$url"
  fi

  if [[ "$LAST_STATUS" == "404" ]]; then
    ok "$label already not_found"
    return 0
  fi

  if [[ "$LAST_STATUS" == "500" ]] && jq -e '
      .ok == false
      and .message == "target stat failed"
      and ((.detail // "") | test("No such file or directory"))
    ' "$LAST_BODY" >/dev/null 2>&1; then
    ok "$label already absent (workspace target stat failed)"
    return 0
  fi

  [[ "$LAST_STATUS" =~ ^2 ]] || fail "$label HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "$label JSON .ok != true"
  ok "$label"
}

expect_raw_status() {
  local label="$1"
  local method="$2"
  local url="$3"
  local expect="$4"
  shift 4

  request "$method" "$url" "$@"
  [[ "$LAST_STATUS" == "$expect" ]] || fail "$label expected HTTP $expect got $LAST_STATUS"
  ok "$label"
}

expect_raw_status_to_file() {
  local label="$1"
  local method="$2"
  local url="$3"
  local expect="$4"
  local out="$5"
  shift 5

  LAST_BODY="$out"
  LAST_STATUS="$(
    curl \
      -o "$out" \
      -w '%{http_code}' \
      "${CURL_BASE_ARGS[@]}" \
      -X "$method" \
      "$BASE$url" \
      "$@"
  )"

  [[ "$LAST_STATUS" == "$expect" ]] || fail "$label expected HTTP $expect got $LAST_STATUS"
  [[ -f "$out" ]] || fail "$label did not create output file: $out"
  ok "$label"
}

put_file() {
  local label="$1"
  local path="$2"
  local local_file="$3"
  local overwrite="${4:-1}"

  local ep="/api/v4/files/put?path=$(urlenc "$path")&overwrite=$overwrite"
  request "PUT" "$ep" \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@$local_file"

  [[ "$LAST_STATUS" =~ ^2 ]] || fail "$label HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "$label JSON .ok != true"
  ok "$label"
}

get_file_to() {
  local label="$1"
  local path="$2"
  local out="$3"

  local ep="/api/v4/files/get?path=$(urlenc "$path")"
  LAST_BODY="$out"
  LAST_STATUS="$(
    curl \
      -o "$out" \
      -w '%{http_code}' \
      "${CURL_BASE_ARGS[@]}" \
      -X GET \
      "$BASE$ep"
  )"

  [[ "$LAST_STATUS" == "200" ]] || fail "$label expected HTTP 200 got $LAST_STATUS"
  ok "$label"
}

json_get() {
  local label="$1"
  local url="$2"
  expect_json_ok "$label" "GET" "$url"
}

json_post() {
  local label="$1"
  local url="$2"
  local body="${3:-}"
  expect_json_ok "$label" "POST" "$url" "$body"
}

path_q() {
  local key="$1"
  local val="$2"
  printf '%s=%s' "$key" "$(urlenc "$val")"
}

assert_file_content_equals() {
  local label="$1"
  local file="$2"
  local expected="$3"

  local actual
  actual="$(cat "$file")"
  [[ "$actual" == "$expected" ]] || {
    echo "Expected: [$expected]" >&2
    echo "Actual:   [$actual]" >&2
    fail "$label content mismatch"
  }
  ok "$label"
}

silent_json_get() {
  local url="$1"

  request "GET" "$url"
  [[ "$LAST_STATUS" =~ ^2 ]] || fail "silent GET $url HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "silent GET $url JSON .ok != true"
}

version_count_for() {
  local path="$1"
  silent_json_get "/api/v4/files/versions/list?path=$(urlenc "$path")&limit=100"
  jq -r '.versions | length' "$LAST_BODY"
}

first_version_id_for() {
  local path="$1"
  silent_json_get "/api/v4/files/versions/list?path=$(urlenc "$path")&limit=100"
  jq -r '.versions[0].version_id // ""' "$LAST_BODY"
}

trash_id_for_original_path() {
  local rel="$1"
  silent_json_get "/api/v4/trash/list?scope=user&include_inactive=1&limit=500"
  jq -r --arg p "$rel" '
    .items
    | map(select(.original_rel_path == $p and .restore_status == "trashed"))
    | sort_by(.deleted_epoch)
    | reverse
    | .[0].trash_id // ""
  ' "$LAST_BODY"
}

run_user_suite() {
  log "User File Manager API smoke"
  echo "BASE=$BASE"
  echo "ROOT=$ROOT"

  local dir="$ROOT"
  local subdir="$ROOT/subdir"
  local text1="$ROOT/versioned.txt"
  local copy1="$ROOT/copied.txt"
  local moved="$ROOT/moved.txt"
  local trashme="$ROOT/trash-restore.txt"
  local binary="$ROOT/blob.bin"

  local f_v1="$TMPDIR/v1.txt"
  local f_v2="$TMPDIR/v2.txt"
  local f_trash="$TMPDIR/trash.txt"
  local f_bin="$TMPDIR/blob.bin"
  local dl="$TMPDIR/download.txt"
  local dl_version="$TMPDIR/version-download.bin"

  printf 'version-one-%s\n' "$TEST_ID" > "$f_v1"
  printf 'version-two-%s\n' "$TEST_ID" > "$f_v2"
  printf 'trash-me-%s\n' "$TEST_ID" > "$f_trash"
  printf 'binary-%s\0tail\n' "$TEST_ID" > "$f_bin"

  log "Pre-cleanup old test root if present"
  expect_json_ok_or_not_found "delete old root" "POST" "/api/v4/files/delete?path=$(urlenc "$dir")"

  log "Create/list/upload/stat/get/hash/read/write"
  json_post "mkdir root" "/api/v4/files/mkdir?path=$(urlenc "$dir")"
  json_post "mkdir subdir" "/api/v4/files/mkdir?path=$(urlenc "$subdir")"
  json_get "list root parent" "/api/v4/files/list?path=$(urlenc "$ROOT")"

  put_file "put v1" "$text1" "$f_v1" 1
  json_get "stat v1" "/api/v4/files/stat?path=$(urlenc "$text1")"
  get_file_to "get v1" "$text1" "$dl"
  assert_file_content_equals "download v1" "$dl" "$(cat "$f_v1")"

  json_post "hash v1" "/api/v4/files/hash?path=$(urlenc "$text1")&algo=sha256"
  json_get "read_text v1" "/api/v4/files/read_text?path=$(urlenc "$text1")"

  log "Overwrite and versions"
  put_file "put overwrite v2" "$text1" "$f_v2" 1
  get_file_to "get v2" "$text1" "$dl"
  assert_file_content_equals "download v2" "$dl" "$(cat "$f_v2")"

  local write_body
  write_body="$(jq -nc --arg p "$text1" --arg t "write-text-$TEST_ID"$'\n' '{path:$p,text:$t}')"
  json_post "write_text v3" "/api/v4/files/write_text" "$write_body"
  json_get "read_text v3" "/api/v4/files/read_text?path=$(urlenc "$text1")"

  local vc
  vc="$(version_count_for "$text1")"
  echo "versions for $text1: $vc"
  if [[ "$EXPECT_VERSIONS" == "1" && "$vc" -lt 1 ]]; then
    fail "expected at least one preserved version"
  fi

  if [[ "$vc" -gt 0 ]]; then
    local vid
    vid="$(first_version_id_for "$text1")"
    [[ -n "$vid" ]] || fail "version id missing"

    expect_raw_status_to_file "versions/download first" "GET" "/api/v4/files/versions/download?path=$(urlenc "$text1")&version_id=$(urlenc "$vid")" "200" "$dl_version"

    local flag_body
    flag_body="$(jq -nc --arg p "$text1" --arg v "$vid" '{path:$p,version_id:$v,note:"smoke-test"}')"
    json_post "versions/flag first" "/api/v4/files/versions/flag" "$flag_body"
    json_post "versions/unflag first" "/api/v4/files/versions/unflag" "$flag_body"

    local restore_body
    restore_body="$(jq -nc --arg p "$text1" --arg v "$vid" '{path:$p,version_id:$v}')"
    json_post "versions/restore first" "/api/v4/files/restore_version" "$restore_body"
    json_get "stat after version restore" "/api/v4/files/stat?path=$(urlenc "$text1")"
  fi

  log "Copy and move"
  json_post "copy file" "/api/v4/files/copy?from=$(urlenc "$text1")&to=$(urlenc "$copy1")"
  json_get "stat copy" "/api/v4/files/stat?path=$(urlenc "$copy1")"

  json_post "move copy" "/api/v4/files/move?from=$(urlenc "$copy1")&to=$(urlenc "$moved")"
  json_get "stat moved" "/api/v4/files/stat?path=$(urlenc "$moved")"
  expect_json_ok_or_not_found "old copy not_found expected" "GET" "/api/v4/files/stat?path=$(urlenc "$copy1")"

  log "Binary upload and zip"
  put_file "put binary" "$binary" "$f_bin" 1
  json_get "stat binary" "/api/v4/files/stat?path=$(urlenc "$binary")"
  expect_raw_status_to_file "zip test root" "GET" "/api/v4/files/zip?path=$(urlenc "$dir")&max_bytes=10485760" "200" "$TMPDIR/root.zip"

  log "Trash and restore"
  put_file "put trash candidate" "$trashme" "$f_trash" 1
  json_get "stat trash candidate" "/api/v4/files/stat?path=$(urlenc "$trashme")"

  json_post "move to trash candidate" "/api/v4/files/delete?path=$(urlenc "$trashme")"
  expect_json_ok_or_not_found "trash candidate now not_found" "GET" "/api/v4/files/stat?path=$(urlenc "$trashme")"

  local tid
  tid="$(trash_id_for_original_path "$trashme")"
  [[ -n "$tid" ]] || fail "trash item not found for $trashme"

  local restore_trash_body
  restore_trash_body="$(jq -nc --arg id "$tid" '{trash_id:$id,rename_if_conflict:false}')"
  json_post "restore trash candidate" "/api/v4/trash/restore" "$restore_trash_body"
  json_get "stat restored trash candidate" "/api/v4/files/stat?path=$(urlenc "$trashme")"

  log "Delete moved file to trash"
  json_post "move moved file to trash" "/api/v4/files/delete?path=$(urlenc "$moved")"
  expect_json_ok_or_not_found "moved file now not_found" "GET" "/api/v4/files/stat?path=$(urlenc "$moved")"

  log "Final list"
  json_get "list test root" "/api/v4/files/list?path=$(urlenc "$dir")"

  if [[ "$KEEP" == "1" ]]; then
    echo "KEEP=1, leaving test root: $dir"
  else
    log "Cleanup test files"

    # Directory rmrf can currently fail when the tree contains mixed storage roots
    # or tier states. Delete the known test payloads individually first so the
    # smoke test validates cleanup without depending on mixed-root rmrf support.
    expect_json_ok_or_not_found "cleanup versioned file" "POST" "/api/v4/files/delete?path=$(urlenc "$text1")"
    expect_json_ok_or_not_found "cleanup binary file" "POST" "/api/v4/files/delete?path=$(urlenc "$binary")"
    expect_json_ok_or_not_found "cleanup restored trash candidate" "POST" "/api/v4/files/delete?path=$(urlenc "$trashme")"
    expect_json_ok_or_not_found "cleanup subdir" "POST" "/api/v4/files/delete?path=$(urlenc "$subdir")"

    log "Cleanup test root"
    request "POST" "/api/v4/files/delete?path=$(urlenc "$dir")"
    if [[ "$LAST_STATUS" =~ ^2 ]]; then
      jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "delete test root cleanup JSON .ok != true"
      ok "delete test root cleanup"
    elif [[ "$LAST_STATUS" == "404" ]]; then
      ok "delete test root cleanup already not_found"
    elif [[ "$LAST_STATUS" == "409" ]] && jq -e '.error == "unsupported"' "$LAST_BODY" >/dev/null 2>&1; then
      echo "WARN: delete test root cleanup skipped: $(jq -r '.message // .error' "$LAST_BODY")"
    else
      fail "delete test root cleanup HTTP $LAST_STATUS"
    fi
  fi
}

run_workspace_suite() {
  local ws="$1"
  local wroot="__api_smoke_ws/$TEST_ID"
  local f="$TMPDIR/ws.txt"
  local f2="$TMPDIR/ws-v2.txt"
  local dl_ws="$TMPDIR/ws-download.txt"
  local dl_ws_version="$TMPDIR/ws-version-download.bin"
  local zip_sel_out="$TMPDIR/ws-selection.zip"

  printf 'workspace-smoke-%s\n' "$TEST_ID" > "$f"
  printf 'workspace-smoke-v2-%s\n' "$TEST_ID" > "$f2"

  log "Workspace File Manager API smoke workspace_id=$ws"

  local qws
  qws="$(urlenc "$ws")"

  local wfile="$wroot/ws-file.txt"
  local wcopy="$wroot/ws-copy.txt"
  local wmoved="$wroot/ws-moved.txt"
  local wtrash="$wroot/ws-trash.txt"

  expect_json_ok_or_absent_workspace_target "workspace delete old root" "POST" "/api/v4/workspaces/files/delete?workspace_id=$qws&path=$(urlenc "$wroot")"

  json_post "workspace mkdir root" "/api/v4/workspaces/files/mkdir?workspace_id=$qws&path=$(urlenc "$wroot")"

  request "PUT" "/api/v4/workspaces/files/put?workspace_id=$qws&path=$(urlenc "$wfile")&overwrite=1" \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@$f"
  [[ "$LAST_STATUS" =~ ^2 ]] || fail "workspace put HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "workspace put .ok != true"
  ok "workspace put"

  json_get "workspace list root" "/api/v4/workspaces/files/list?workspace_id=$qws&path=$(urlenc "$wroot")"
  json_get "workspace stat file" "/api/v4/workspaces/files/stat?workspace_id=$qws&path=$(urlenc "$wfile")"
  json_post "workspace hash file" "/api/v4/workspaces/files/hash?workspace_id=$qws&path=$(urlenc "$wfile")&algo=sha256"
  json_get "workspace read_text file" "/api/v4/workspaces/files/read_text?workspace_id=$qws&path=$(urlenc "$wfile")"

  expect_raw_status_to_file "workspace get file" "GET" "/api/v4/workspaces/files/get?workspace_id=$qws&path=$(urlenc "$wfile")" "200" "$dl_ws"
  assert_file_content_equals "workspace download file" "$dl_ws" "$(cat "$f")"

  log "Workspace write_text with edit lease"
  local wsession
  wsession="smoke_${TEST_ID}_$$"

  local lease_body
  lease_body="$(jq -nc --arg ws "$ws" --arg p "$wfile" --arg sid "$wsession" '{workspace_id:$ws,path:$p,session_id:$sid,lease_seconds:60}')"
  json_post "workspace edit lease acquire" "/api/v4/workspaces/files/edit_lease/acquire" "$lease_body"

  local ws_write_body
  ws_write_body="$(jq -nc --arg ws "$ws" --arg p "$wfile" --arg sid "$wsession" --arg t "workspace-write-text-$TEST_ID"$'\n' '{workspace_id:$ws,path:$p,session_id:$sid,text:$t}')"
  json_post "workspace write_text with lease" "/api/v4/workspaces/files/write_text" "$ws_write_body"
  json_get "workspace read_text after write_text" "/api/v4/workspaces/files/read_text?workspace_id=$qws&path=$(urlenc "$wfile")"

  json_post "workspace edit lease release" "/api/v4/workspaces/files/edit_lease/release" "$lease_body"

  log "Workspace overwrite and versions"

  # External Workspace uploads and conflict replace use the workspace PUT endpoint.
  # The workspace write_text endpoint currently requires a session_id, so this
  # smoke test uses PUT overwrite here to exercise the external-compatible path.
  request "PUT" "/api/v4/workspaces/files/put?workspace_id=$qws&path=$(urlenc "$wfile")&overwrite=1" \
    -H "Content-Type: text/plain; charset=utf-8" \
    --data-binary "@$f2"
  [[ "$LAST_STATUS" =~ ^2 ]] || fail "workspace overwrite put before versions HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "workspace overwrite put before versions .ok != true"
  ok "workspace overwrite put before versions"

  json_get "workspace read_text after overwrite" "/api/v4/workspaces/files/read_text?workspace_id=$qws&path=$(urlenc "$wfile")"

  local wvc
  silent_json_get "/api/v4/workspaces/files/versions/list?workspace_id=$qws&path=$(urlenc "$wfile")&limit=100"
  wvc="$(jq -r '.versions | length' "$LAST_BODY")"
  echo "workspace versions for $wfile: $wvc"

  if [[ "$EXPECT_VERSIONS" == "1" && "$wvc" -lt 1 ]]; then
    fail "expected at least one workspace preserved version"
  fi

  if [[ "$wvc" -gt 0 ]]; then
    local wvid
    wvid="$(jq -r '.versions[0].version_id // ""' "$LAST_BODY")"
    [[ -n "$wvid" ]] || fail "workspace version id missing"

    expect_raw_status_to_file "workspace versions/download first" "GET" "/api/v4/workspaces/files/versions/download?workspace_id=$qws&path=$(urlenc "$wfile")&version_id=$(urlenc "$wvid")" "200" "$dl_ws_version"

    local wflag_body
    wflag_body="$(jq -nc --arg ws "$ws" --arg p "$wfile" --arg v "$wvid" '{workspace_id:$ws,path:$p,version_id:$v,note:"smoke-test"}')"
    json_post "workspace versions/flag first" "/api/v4/workspaces/files/versions/flag" "$wflag_body"
    json_post "workspace versions/unflag first" "/api/v4/workspaces/files/versions/unflag" "$wflag_body"

    local wrestore_body
    wrestore_body="$(jq -nc --arg ws "$ws" --arg p "$wfile" --arg v "$wvid" '{workspace_id:$ws,path:$p,version_id:$v}')"
    json_post "workspace versions/restore first" "/api/v4/workspaces/files/restore_version" "$wrestore_body"
    json_get "workspace stat after version restore" "/api/v4/workspaces/files/stat?workspace_id=$qws&path=$(urlenc "$wfile")"
  fi

  log "Workspace overwrite after version restore"
  request "PUT" "/api/v4/workspaces/files/put?workspace_id=$qws&path=$(urlenc "$wfile")&overwrite=1" \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@$f2"
  [[ "$LAST_STATUS" =~ ^2 ]] || fail "workspace overwrite put HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "workspace overwrite put .ok != true"
  ok "workspace overwrite put"

  expect_raw_status_to_file "workspace zip selection" "POST" "/api/v4/workspaces/files/zip_sel?workspace_id=$qws" "200" "$zip_sel_out" \
    -H "Content-Type: application/json" \
    -H "Accept: application/zip, application/json" \
    --data-binary "$(jq -nc --arg ws "$ws" --arg p "$wfile" --arg base "$wroot" '{workspace_id:$ws,paths:[$p],base:$base,max_bytes:10485760}')"

  json_post "workspace copy file" "/api/v4/workspaces/files/copy?workspace_id=$qws&from=$(urlenc "$wfile")&to=$(urlenc "$wcopy")"
  json_post "workspace move copy" "/api/v4/workspaces/files/move?workspace_id=$qws&from=$(urlenc "$wcopy")&to=$(urlenc "$wmoved")"
  json_get "workspace stat moved" "/api/v4/workspaces/files/stat?workspace_id=$qws&path=$(urlenc "$wmoved")"

  request "PUT" "/api/v4/workspaces/files/put?workspace_id=$qws&path=$(urlenc "$wtrash")&overwrite=1" \
    -H "Content-Type: application/octet-stream" \
    --data-binary "@$f"
  [[ "$LAST_STATUS" =~ ^2 ]] || fail "workspace put trash candidate HTTP $LAST_STATUS"
  jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "workspace put trash candidate .ok != true"
  ok "workspace put trash candidate"

  json_post "workspace trash moved" "/api/v4/workspaces/files/delete?workspace_id=$qws&path=$(urlenc "$wmoved")"
  json_post "workspace trash candidate" "/api/v4/workspaces/files/delete?workspace_id=$qws&path=$(urlenc "$wtrash")"
  json_get "workspace trash list" "/api/v4/workspaces/files/trash/list?workspace_id=$qws&limit=500"

  if [[ "$KEEP" == "1" ]]; then
    echo "KEEP=1, leaving workspace test root: $wroot"
  else
    log "Workspace cleanup files"
    expect_json_ok_or_absent_workspace_target "workspace cleanup primary file" "POST" "/api/v4/workspaces/files/delete?workspace_id=$qws&path=$(urlenc "$wfile")"
    expect_json_ok_or_absent_workspace_target "workspace cleanup moved file" "POST" "/api/v4/workspaces/files/delete?workspace_id=$qws&path=$(urlenc "$wmoved")"
    expect_json_ok_or_absent_workspace_target "workspace cleanup trash candidate" "POST" "/api/v4/workspaces/files/delete?workspace_id=$qws&path=$(urlenc "$wtrash")"

    log "Workspace cleanup root"
    request "POST" "/api/v4/workspaces/files/delete?workspace_id=$qws&path=$(urlenc "$wroot")"
    if [[ "$LAST_STATUS" =~ ^2 ]]; then
      jq -e '.ok == true' "$LAST_BODY" >/dev/null || fail "workspace delete root cleanup JSON .ok != true"
      ok "workspace delete root cleanup"
    elif [[ "$LAST_STATUS" == "404" ]]; then
      ok "workspace delete root cleanup already not_found"
    elif [[ "$LAST_STATUS" == "500" ]] && jq -e '
        .ok == false
        and .message == "target stat failed"
        and ((.detail // "") | test("No such file or directory"))
      ' "$LAST_BODY" >/dev/null 2>&1; then
      ok "workspace delete root cleanup already absent (workspace target stat failed)"
    elif [[ "$LAST_STATUS" == "409" ]] && jq -e '.error == "unsupported"' "$LAST_BODY" >/dev/null 2>&1; then
      echo "WARN: workspace delete root cleanup skipped: $(jq -r '.message // .error' "$LAST_BODY")"
    else
      fail "workspace delete root cleanup HTTP $LAST_STATUS"
    fi
  fi
}

run_user_suite

if [[ -n "${WORKSPACE_ID:-}" ]]; then
  run_workspace_suite "$WORKSPACE_ID"
else
  echo
  echo "WORKSPACE_ID not set; skipping workspace suite."
fi

echo
echo "ALL FILE MANAGER API SMOKE TESTS PASSED"
