#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(git rev-parse --show-toplevel)"
cd "$REPO_ROOT"

BUNDLED_DIR="apps/bundled"

need() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing dependency: $1" >&2
    return 1
  }
}

need jq
need python3
need unzip

build_one() (
  local appdir="$1"
  local appname
  appname="$(basename "$appdir")"

  local src="$appdir/src"
  local man="$src/manifest.json"
  local www="$src/www"

  if [[ ! -f "$man" || ! -d "$www" ]]; then
    echo "[SKIP] $appname: missing src/manifest.json or src/www/"
    return 0
  fi

  local id ver
  id="$(jq -r '.id // empty' "$man")"
  ver="$(jq -r '.version // empty' "$man")"

  if [[ -z "$id" || -z "$ver" ]]; then
    echo "[ERROR] $appname: manifest.json missing .id or .version" >&2
    return 2
  fi

  if [[ "$id" != "$appname" ]]; then
    echo "[WARN] $appname: manifest id='$id' differs from directory name '$appname'"
  fi

  local out_abs="${REPO_ROOT}/${appdir}/${id}-${ver}.zip"
  local tmp_zip
  tmp_zip="$(mktemp --suffix=.zip)"

  cleanup() {
    rm -f "$tmp_zip"
  }
  trap cleanup EXIT

  python3 - "$src" "$tmp_zip" <<'PYZIP'
from __future__ import annotations

import os
import stat
import sys
import zipfile
from pathlib import Path

source = Path(sys.argv[1])
output = Path(sys.argv[2])

# ZIP timestamps cannot represent dates before 1980. A fixed timestamp keeps
# release archives reproducible instead of inheriting source-file mtimes.
fixed_time = (2000, 1, 1, 0, 0, 0)

manifest = source / "manifest.json"
www = source / "www"

entries = [manifest]
entries.extend(
    sorted(
        www.rglob("*"),
        key=lambda path: path.relative_to(source).as_posix(),
    )
)

with zipfile.ZipFile(
    output,
    mode="w",
    compression=zipfile.ZIP_DEFLATED,
    compresslevel=9,
) as archive:
    for path in entries:
        relative = path.relative_to(source).as_posix()

        # Do not silently package symlink targets. Rejecting symlinks prevents
        # unexpected files outside the bundled app tree from entering a ZIP.
        if path.is_symlink():
            raise SystemExit(f"ERROR: symbolic link is not allowed: {path}")

        mode = stat.S_IMODE(path.stat().st_mode)

        if path.is_dir():
            info = zipfile.ZipInfo(
                relative.rstrip("/") + "/",
                date_time=fixed_time,
            )
            info.create_system = 3
            info.external_attr = (
                (stat.S_IFDIR | mode) << 16
            ) | 0x10
            info.compress_type = zipfile.ZIP_STORED
            archive.writestr(info, b"")
            continue

        if not path.is_file():
            raise SystemExit(f"ERROR: unsupported filesystem entry: {path}")

        info = zipfile.ZipInfo(relative, date_time=fixed_time)
        info.create_system = 3
        info.external_attr = (stat.S_IFREG | mode) << 16
        info.compress_type = zipfile.ZIP_DEFLATED

        archive.writestr(
            info,
            path.read_bytes(),
            compress_type=zipfile.ZIP_DEFLATED,
            compresslevel=9,
        )
PYZIP

  unzip -tq "$tmp_zip" >/dev/null 2>&1 || {
    echo "[ERROR] $appname: generated ZIP is corrupt: $tmp_zip" >&2
    return 3
  }

  local names
  names="$(unzip -Z -1 "$tmp_zip" | tr -d '\r')"

  printf '%s\n' "$names" | grep -qx 'manifest.json' || {
    echo "[ERROR] $appname: ZIP missing manifest.json" >&2
    return 3
  }

  printf '%s\n' "$names" | grep -q '^www/' || {
    echo "[ERROR] $appname: ZIP missing www/" >&2
    return 3
  }

  if [[ "$appname" == "filemgr" ]]; then
    printf '%s\n' "$names" |
      grep -q '^www/icons/filetypes/default.svg$' || {
        echo "[ERROR] filemgr missing default icon" >&2
        return 4
      }
  fi

  local archive_state
  archive_state="$(
    python3 - "$out_abs" "$tmp_zip" <<'PYCOMPARE'
from __future__ import annotations

import hashlib
import stat
import sys
import zipfile
from pathlib import Path

old_path = Path(sys.argv[1])
new_path = Path(sys.argv[2])

def logical_contents(path: Path):
    entries = {}

    with zipfile.ZipFile(path) as archive:
        for info in archive.infolist():
            name = info.filename

            # Directory records, timestamps, compression settings and Unix
            # metadata may differ between ZIP writers. Compare packaged file
            # paths and bytes only to avoid false Git modifications.
            if info.is_dir() or name.endswith("/"):
                continue

            if name in entries:
                raise SystemExit(
                    f"ERROR: duplicate ZIP entry in {path}: {name}"
                )

            data = archive.read(info)
            entries[name] = (
                len(data),
                hashlib.sha256(data).hexdigest(),
            )

    return entries

if not old_path.is_file():
    print("new")
elif logical_contents(old_path) == logical_contents(new_path):
    print("unchanged")
else:
    print("changed")
PYCOMPARE
  )"

  case "$archive_state" in
    unchanged)
      printf "[UNCHANGED] %-14s -> %s\n" \
        "$appname" "$(basename "$out_abs")"
      ;;

    new|changed)
      mv -f "$tmp_zip" "$out_abs"
      trap - EXIT

      printf "[OK]        %-14s -> %s (%s bytes)\n" \
        "$appname" \
        "$(basename "$out_abs")" \
        "$(stat -c %s "$out_abs")"
      ;;

    *)
      echo "[ERROR] $appname: unexpected comparison result: $archive_state" >&2
      return 5
      ;;
  esac
)

for appdir in "$BUNDLED_DIR"/*; do
  [[ -d "$appdir" ]] || continue
  build_one "$appdir"
done
