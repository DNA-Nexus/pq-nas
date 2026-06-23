#!/usr/bin/env python3
from pathlib import Path
import re
import sys

routes_path = Path("server/src/routes_v5.cc")
main_path = Path("server/src/main.cpp")

for p in (routes_path, main_path):
    if not p.exists():
        print(f"ERROR: missing {p}", file=sys.stderr)
        sys.exit(1)

def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def add_includes(s: str, includes: list[str]) -> str:
    missing = [inc for inc in includes if inc not in s]
    if not missing:
        return s

    lines = s.splitlines(True)
    last_include = -1
    for i, line in enumerate(lines[:200]):
        if line.startswith("#include "):
            last_include = i

    if last_include < 0:
        die("include insertion anchor not found")

    insert = "".join(inc + "\n" for inc in missing)
    lines.insert(last_include + 1, insert)
    return "".join(lines)

needed_includes = [
    "#include <cerrno>",
    "#include <chrono>",
    "#include <cstring>",
    "#include <fcntl.h>",
    "#include <sys/file.h>",
    "#include <unistd.h>",
]

# ---------------------------------------------------------------------
# routes_v5.cc: shared file lock helper + unique tmp + lock around all
# opaque_enrollments.json read/modify/write sections + R-3 rollback.
# ---------------------------------------------------------------------
s = routes_path.read_text()
s = add_includes(s, needed_includes)

mutex_anchor = '''static std::mutex& routes_v5_opaque_enrollments_file_mu() {
    static std::mutex mu;
    return mu;
}

'''

routes_file_lock_class = r'''class RoutesV5OpaqueEnrollmentsFileLock {
public:
    RoutesV5OpaqueEnrollmentsFileLock(const std::string& enrollments_path, std::string* err) {
        if (err) err->clear();

        std::error_code ec;
        const std::filesystem::path target(enrollments_path);
        const std::filesystem::path parent = target.parent_path();
        if (!parent.empty()) {
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                if (err) *err = "create_lock_parent_failed: " + ec.message();
                return;
            }
        }

        const std::filesystem::path lock_path = target.string() + ".lock";

#ifdef O_CLOEXEC
        const int flags = O_CREAT | O_RDWR | O_CLOEXEC;
#else
        const int flags = O_CREAT | O_RDWR;
#endif

        fd_ = ::open(lock_path.c_str(), flags, 0600);
        if (fd_ < 0) {
            if (err) *err = std::string("open_lock_failed: ") + std::strerror(errno);
            return;
        }

        if (::flock(fd_, LOCK_EX) != 0) {
            if (err) *err = std::string("flock_failed: ") + std::strerror(errno);
            ::close(fd_);
            fd_ = -1;
            return;
        }

        ok_ = true;
    }

    RoutesV5OpaqueEnrollmentsFileLock(const RoutesV5OpaqueEnrollmentsFileLock&) = delete;
    RoutesV5OpaqueEnrollmentsFileLock& operator=(const RoutesV5OpaqueEnrollmentsFileLock&) = delete;

    ~RoutesV5OpaqueEnrollmentsFileLock() {
        if (fd_ >= 0) {
            (void)::flock(fd_, LOCK_UN);
            (void)::close(fd_);
        }
    }

    bool ok() const { return ok_; }

private:
    int fd_ = -1;
    bool ok_ = false;
};


'''

if "class RoutesV5OpaqueEnrollmentsFileLock" not in s:
    if mutex_anchor not in s:
        die("routes_v5 mutex anchor not found")
    s = s.replace(mutex_anchor, mutex_anchor + routes_file_lock_class, 1)
    print("routes_v5: inserted file lock RAII helper")
else:
    print("routes_v5: file lock helper already present")

old_tmp = 'const std::filesystem::path tmp = target.string() + ".tmp";'
new_tmp = '''const std::filesystem::path tmp =
        target.string() +
        ".tmp." +
        std::to_string(static_cast<long long>(::getpid())) +
        "." +
        std::to_string(static_cast<long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));'''

if old_tmp in s:
    s = s.replace(old_tmp, new_tmp, 1)
    print("routes_v5: replaced static .tmp filename with unique tmp filename")
elif '.tmp." +' in s:
    print("routes_v5: unique tmp filename already present")
else:
    die("routes_v5 tmp filename anchor not found")

# Add file lock immediately after every existing in-process mutex lock.
lock_pat = re.compile(r'^([ \t]*)std::lock_guard<std::mutex> lock\(routes_v5_opaque_enrollments_file_mu\(\)\);\n', re.M)

def add_file_lock_after_mutex(m: re.Match) -> str:
    start = m.end()
    window = s[start:start + 700]
    if "RoutesV5OpaqueEnrollmentsFileLock opaque_enrollments_file_lock" in window:
        return m.group(0)

    indent = m.group(1)
    return (
        m.group(0) +
        "\n" +
        f"{indent}std::string opaque_enrollments_file_lock_err;\n" +
        f"{indent}RoutesV5OpaqueEnrollmentsFileLock opaque_enrollments_file_lock(\n" +
        f"{indent}    routes_v5_opaque_enrollments_path(ctx),\n" +
        f"{indent}    &opaque_enrollments_file_lock_err);\n" +
        f"{indent}if (!opaque_enrollments_file_lock.ok()) {{\n" +
        f"{indent}    reply_json(res, 500, json{{{{\"ok\", false}}, {{\"error\", \"server_error\"}}, {{\"message\", \"opaque_enrollments_lock_failed\"}}, {{\"detail\", opaque_enrollments_file_lock_err}}}}.dump());\n" +
        f"{indent}    return;\n" +
        f"{indent}}}\n"
    )

s2 = lock_pat.sub(add_file_lock_after_mutex, s)
if s2 != s:
    print(f"routes_v5: added file lock after {s.count('std::lock_guard<std::mutex> lock(routes_v5_opaque_enrollments_file_mu());')} mutex lock site(s)")
s = s2

# R-3: rollback just-created force-reset token if credential disable/save fails.
failure_anchor = '''        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.force_reset", "deny", login, actor_fp, "opaque_credentials_save_failed_after_token_create");
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "opaque_credentials_save_failed_after_token_create"},
                {"note", "reset_token_was_created_but_old_credential_may_still_be_enabled"}
            }.dump());
            return;
        }
'''

failure_replacement = '''        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            bool reset_token_rollback_ok = false;
            std::string reset_token_rollback_err;

            {
                std::lock_guard<std::mutex> lock(routes_v5_opaque_enrollments_file_mu());

                std::string opaque_enrollments_file_lock_err;
                RoutesV5OpaqueEnrollmentsFileLock opaque_enrollments_file_lock(
                    routes_v5_opaque_enrollments_path(ctx),
                    &opaque_enrollments_file_lock_err);
                if (!opaque_enrollments_file_lock.ok()) {
                    reset_token_rollback_err = "opaque_enrollments_lock_failed: " + opaque_enrollments_file_lock_err;
                } else {
                    std::string lerr;
                    json rollback_doc = routes_v5_load_opaque_enrollments_no_lock(enrollments_path, &lerr);
                    if (!lerr.empty()) {
                        reset_token_rollback_err = "opaque_enrollments_load_failed: " + lerr;
                    } else {
                        bool changed = false;
                        for (auto& rollback_rec : rollback_doc["tokens"]) {
                            if (!rollback_rec.is_object()) continue;
                            if (rollback_rec.value("token_hash", "") != token_hash) continue;

                            rollback_rec["used_at"] = now;
                            rollback_rec["invalidated_at"] = now;
                            rollback_rec["invalidated_reason"] = "force_reset_credential_disable_failed";
                            changed = true;
                            break;
                        }

                        if (changed) {
                            std::string serr;
                            if (!routes_v5_save_opaque_enrollments_no_lock(enrollments_path, rollback_doc, &serr)) {
                                reset_token_rollback_err = "opaque_enrollments_save_failed: " + serr;
                            } else {
                                reset_token_rollback_ok = true;
                            }
                        } else {
                            reset_token_rollback_err = "force_reset_token_not_found_for_rollback";
                        }
                    }
                }
            }

            routes_v5_audit_password(
                ctx,
                req,
                "opaque.force_reset",
                "deny",
                login,
                actor_fp,
                reset_token_rollback_ok
                    ? "opaque_credentials_save_failed_after_token_rollback"
                    : "opaque_credentials_save_failed_after_token_rollback_failed");

            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "opaque_credentials_save_failed_after_token_create"},
                {"reset_token_invalidated", reset_token_rollback_ok},
                {"rollback_error", reset_token_rollback_err}
            }.dump());
            return;
        }
'''

if failure_anchor in s:
    s = s.replace(failure_anchor, failure_replacement, 1)
    print("routes_v5: added force-reset token rollback on credential save failure")
elif "reset_token_invalidated" in s:
    print("routes_v5: force-reset rollback already present")
else:
    die("routes_v5 force-reset failure anchor not found")

routes_path.write_text(s)

# ---------------------------------------------------------------------
# main.cpp: shared file lock helper around its enrollment invalidation,
# plus unique tmp filename.
# ---------------------------------------------------------------------
m = main_path.read_text()
m = add_includes(m, needed_includes)

old_tmp_main = 'const std::filesystem::path tmp = target.string() + ".tmp";'
new_tmp_main = '''const std::filesystem::path tmp =
            target.string() +
            ".tmp." +
            std::to_string(static_cast<long long>(::getpid())) +
            "." +
            std::to_string(static_cast<long long>(
                std::chrono::steady_clock::now().time_since_epoch().count()));'''

if old_tmp_main in m:
    m = m.replace(old_tmp_main, new_tmp_main, 1)
    print("main: replaced static .tmp filename with unique tmp filename")
elif '.tmp." +' in m:
    print("main: unique tmp filename already present")
else:
    die("main tmp filename anchor not found")

path_helper_anchor = '''    auto opaque_enrollments_path_for_admin_status = [&]() -> std::string {
        const char* raw = std::getenv("PQNAS_OPAQUE_ENROLLMENTS_PATH");
        const std::string env_path = trim_ascii_for_opaque_enrollments(raw ? raw : "");
        if (!env_path.empty()) return env_path;

        if (!users_path.empty()) {
            std::filesystem::path p(users_path);
            return (p.parent_path() / "opaque_enrollments.json").string();
        }

        return "/var/lib/pqnas/opaque_enrollments.json";
    };

'''

main_lock_class = r'''    class AdminStatusOpaqueEnrollmentsFileLock {
    public:
        AdminStatusOpaqueEnrollmentsFileLock(const std::string& enrollments_path, std::string* err) {
            if (err) err->clear();

            std::error_code ec;
            const std::filesystem::path target(enrollments_path);
            const std::filesystem::path parent = target.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent, ec);
                if (ec) {
                    if (err) *err = "create_lock_parent_failed: " + ec.message();
                    return;
                }
            }

            const std::filesystem::path lock_path = target.string() + ".lock";

#ifdef O_CLOEXEC
            const int flags = O_CREAT | O_RDWR | O_CLOEXEC;
#else
            const int flags = O_CREAT | O_RDWR;
#endif

            fd_ = ::open(lock_path.c_str(), flags, 0600);
            if (fd_ < 0) {
                if (err) *err = std::string("open_lock_failed: ") + std::strerror(errno);
                return;
            }

            if (::flock(fd_, LOCK_EX) != 0) {
                if (err) *err = std::string("flock_failed: ") + std::strerror(errno);
                ::close(fd_);
                fd_ = -1;
                return;
            }

            ok_ = true;
        }

        AdminStatusOpaqueEnrollmentsFileLock(const AdminStatusOpaqueEnrollmentsFileLock&) = delete;
        AdminStatusOpaqueEnrollmentsFileLock& operator=(const AdminStatusOpaqueEnrollmentsFileLock&) = delete;

        ~AdminStatusOpaqueEnrollmentsFileLock() {
            if (fd_ >= 0) {
                (void)::flock(fd_, LOCK_UN);
                (void)::close(fd_);
            }
        }

        bool ok() const { return ok_; }

    private:
        int fd_ = -1;
        bool ok_ = false;
    };


'''

if "class AdminStatusOpaqueEnrollmentsFileLock" not in m:
    if path_helper_anchor not in m:
        die("main opaque_enrollments_path helper anchor not found")
    m = m.replace(path_helper_anchor, path_helper_anchor + main_lock_class, 1)
    print("main: inserted file lock RAII helper")
else:
    print("main: file lock helper already present")

path_line = '''            const std::string path = opaque_enrollments_path_for_admin_status();
            const long now = static_cast<long>(std::time(nullptr));

'''

path_line_repl = '''            const std::string path = opaque_enrollments_path_for_admin_status();

            std::string opaque_enrollments_file_lock_err;
            AdminStatusOpaqueEnrollmentsFileLock opaque_enrollments_file_lock(
                path,
                &opaque_enrollments_file_lock_err);
            if (!opaque_enrollments_file_lock.ok()) {
                if (err) *err = "opaque_enrollments_lock_failed: " + opaque_enrollments_file_lock_err;
                return false;
            }

            const long now = static_cast<long>(std::time(nullptr));

'''

if path_line in m:
    m = m.replace(path_line, path_line_repl, 1)
    print("main: added file lock around revoke invalidation")
elif "opaque_enrollments_lock_failed" in m:
    print("main: revoke invalidation file lock already present")
else:
    die("main invalidation path anchor not found")

main_path.write_text(m)

print("done")
