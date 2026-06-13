#!/usr/bin/env python3
from pathlib import Path
import sys

path = Path("server/src/trash_service.cpp")
if not path.exists():
    print(f"ERROR: missing {path}", file=sys.stderr)
    sys.exit(1)

text = path.read_text()

old = '''static bool remove_path_recursive_local(const std::filesystem::path& p, std::string* err) {
    if (err) err->clear();
    std::error_code ec;

    auto st = std::filesystem::symlink_status(p, ec);
    if (ec) {
        if (err) *err = "symlink_status failed: " + ec.message();
        return false;
    }
    if (!std::filesystem::exists(st)) {
        return true;
    }
    if (std::filesystem::is_symlink(st)) {
        if (err) *err = "symlinks not supported";
        return false;
    }

    if (std::filesystem::is_directory(st)) {
        std::filesystem::remove_all(p, ec);
    } else {
        std::filesystem::remove(p, ec);
    }

    if (ec) {
        if (err) *err = "remove failed: " + ec.message();
        return false;
    }
    return true;
}
'''

new = '''static bool remove_path_recursive_local(const std::filesystem::path& p, std::string* err) {
    if (err) err->clear();
    std::error_code ec;

    auto st = std::filesystem::symlink_status(p, ec);
    if (ec) {
        // Idempotent purge behavior:
        //
        // Snapshot rollback, manual operator cleanup, or a previous partial purge can
        // leave a trash metadata row pointing at a payload path that no longer exists.
        // In purge mode that is not a filesystem failure; the payload is already gone,
        // so the caller should be allowed to mark the trash row as purged.
        if (ec == std::errc::no_such_file_or_directory) {
            return true;
        }

        if (err) *err = "symlink_status failed: " + ec.message();
        return false;
    }
    if (!std::filesystem::exists(st)) {
        return true;
    }
    if (std::filesystem::is_symlink(st)) {
        if (err) *err = "symlinks not supported";
        return false;
    }

    if (std::filesystem::is_directory(st)) {
        std::filesystem::remove_all(p, ec);
    } else {
        std::filesystem::remove(p, ec);
    }

    if (ec) {
        if (err) *err = "remove failed: " + ec.message();
        return false;
    }
    return true;
}
'''

count = text.count(old)
if count != 1:
    print(f"ERROR: expected exactly one remove_path_recursive_local anchor, found {count}", file=sys.stderr)
    sys.exit(1)

text = text.replace(old, new, 1)
path.write_text(text)
print(f"patched: {path}")
