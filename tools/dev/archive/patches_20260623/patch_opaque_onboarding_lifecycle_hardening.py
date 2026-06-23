#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("server/src/routes_v5.cc")
if not p.exists():
    print(f"ERROR: missing {p}", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def replace_exact(desc, old, new):
    global s
    if old in s:
        s = s.replace(old, new, 1)
        print(f"patched: {desc}")
        return True
    print(f"unchanged or missing: {desc}")
    return False

# ---------------------------------------------------------------------
# F-3: atomic save for opaque_enrollments.json
# ---------------------------------------------------------------------
old_save = '''static bool routes_v5_save_opaque_enrollments_no_lock(const std::string& path,
                                                      const json& doc,
                                                      std::string* err) {
    if (err) err->clear();

    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
    if (ec) {
        if (err) *err = "create_directories_failed: " + ec.message();
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        if (err) *err = "open_for_write_failed";
        return false;
    }

    out << doc.dump(2) << "\\n";
    if (!out) {
        if (err) *err = "write_failed";
        return false;
    }

    return true;
}
'''

new_save = '''static bool routes_v5_save_opaque_enrollments_no_lock(const std::string& path,
                                                      const json& doc,
                                                      std::string* err) {
    if (err) err->clear();

    std::error_code ec;
    const std::filesystem::path target(path);
    std::filesystem::create_directories(target.parent_path(), ec);
    if (ec) {
        if (err) *err = "create_directories_failed: " + ec.message();
        return false;
    }

    const std::filesystem::path tmp = target.string() + ".tmp";

    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            if (err) *err = "open_tmp_for_write_failed";
            return false;
        }

        out << doc.dump(2) << "\\n";
        out.flush();
        out.close();

        if (!out) {
            std::error_code rm_ec;
            std::filesystem::remove(tmp, rm_ec);
            if (err) *err = "write_tmp_failed";
            return false;
        }
    }

    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);
        if (err) *err = "atomic_rename_failed: " + ec.message();
        return false;
    }

    return true;
}
'''

if old_save in s:
    replace_exact("atomic opaque enrollment save", old_save, new_save)
elif "open_tmp_for_write_failed" in s and "atomic_rename_failed" in s:
    print("unchanged: atomic opaque enrollment save already present")
else:
    die("opaque enrollment save function did not match old or new form")

# ---------------------------------------------------------------------
# Shared helper: invalidate active setup/reset tokens for login/fingerprint.
# Used by F-10 now and F-1 later.
# ---------------------------------------------------------------------
prune_func = '''static void routes_v5_prune_opaque_enrollments_doc(json& doc, long now) {
    if (!doc.contains("tokens") || !doc["tokens"].is_array()) {
        doc["tokens"] = json::array();
        return;
    }

    json kept = json::array();

    for (const auto& rec : doc["tokens"]) {
        if (!rec.is_object()) continue;

        const long expires_at = rec.value("expires_at", 0L);
        const long used_at = rec.value("used_at", 0L);

        if (expires_at > 0 && expires_at + 86400 < now) continue;
        if (used_at > 0 && used_at + 86400 < now) continue;

        kept.push_back(rec);
    }

    doc["tokens"] = kept;
}
'''

helper_func = prune_func + '''

static std::size_t routes_v5_invalidate_active_opaque_enrollment_tokens(json& doc,
                                                                        const std::string& login,
                                                                        const std::string& fingerprint,
                                                                        long now,
                                                                        const std::string& reason) {
    if (!doc.contains("tokens") || !doc["tokens"].is_array()) {
        doc["tokens"] = json::array();
        return 0;
    }

    std::size_t changed = 0;

    for (auto& rec : doc["tokens"]) {
        if (!rec.is_object()) continue;

        const bool login_match = login.empty() || rec.value("login", "") == login;
        const bool fingerprint_match = fingerprint.empty() || rec.value("fingerprint", "") == fingerprint;
        if (!login_match || !fingerprint_match) continue;

        const long used_at = rec.value("used_at", 0L);
        const long expires_at = rec.value("expires_at", 0L);
        if (used_at > 0 || expires_at <= now) continue;

        rec["used_at"] = now;
        rec["invalidated_at"] = now;
        rec["invalidated_reason"] = reason.empty() ? "invalidated" : reason;
        ++changed;
    }

    return changed;
}
'''

if "routes_v5_invalidate_active_opaque_enrollment_tokens" not in s:
    if prune_func not in s:
        die("prune function anchor not found")
    s = s.replace(prune_func, helper_func, 1)
    print("patched: added active enrollment-token invalidation helper")
else:
    print("unchanged: active enrollment-token invalidation helper already present")

# ---------------------------------------------------------------------
# F-10: New setup/reset token invalidates old active tokens for same user.
# Current code uses doc["tokens"].push_back(json{...}) inline.
# ---------------------------------------------------------------------
old_token_push_anchor = '''            routes_v5_prune_opaque_enrollments_doc(doc, now);

            doc["tokens"].push_back(json{
'''
new_token_push_anchor = '''            routes_v5_prune_opaque_enrollments_doc(doc, now);

            routes_v5_invalidate_active_opaque_enrollment_tokens(
                doc,
                login,
                fingerprint,
                now,
                "replaced_by_new_token");

            doc["tokens"].push_back(json{
'''

if old_token_push_anchor in s:
    s = s.replace(old_token_push_anchor, new_token_push_anchor, 1)
    print("patched: invalidate old active tokens before creating a new token")
elif '"replaced_by_new_token"' in s:
    print("unchanged: old-token invalidation already present")
else:
    die("inline token push anchor not found")

# ---------------------------------------------------------------------
# F-4/F-5: OPAQUE-created users always start disabled.
# Ignore client status entirely.
# ---------------------------------------------------------------------
old_status = '''        // Default to disabled because the user cannot log in until an OPAQUE
        // credential has been enrolled. UsersRegistry currently supports the
        // existing enabled/disabled/revoked status model; OPAQUE enrollment
        // finish can promote the user to enabled after the credential is stored.
        std::string status =
            routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "status")));
        if (status.empty()) status = "disabled";
'''

new_status = '''        // OPAQUE-created users must always start disabled. The enrollment
        // finish flow is the only path that may promote the user to enabled
        // after a real OPAQUE credential has been stored.
        const std::string status = "disabled";
'''

if old_status in s:
    s = s.replace(old_status, new_status, 1)
    print("patched: force opaque-create status to disabled")
elif 'const std::string status = "disabled";' in s:
    print("unchanged: opaque-create already forces disabled")
else:
    die("opaque-create status block not found")

old_status_validation = '''        if (status != "enabled" && status != "disabled" && status != "pending") {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_status"}}.dump());
            return;
        }

'''

if old_status_validation in s:
    s = s.replace(old_status_validation, "", 1)
    print("patched: removed obsolete status validation")
elif "invalid_status" not in s:
    print("unchanged: obsolete status validation already absent")
else:
    die("invalid_status still present but exact block did not match")

# ---------------------------------------------------------------------
# F-9: Consume token before storing OPAQUE credential.
# Old code marked token used after credential + user saves.
# ---------------------------------------------------------------------
old_late_mark = '''        (*token_rec)["used_at"] = now;

        std::string serr;
        if (!routes_v5_save_opaque_enrollments_no_lock(enrollments_path, doc, &serr)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_finish", "deny", login, fingerprint, "enrollment_token_mark_used_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "enrollment_token_mark_used_failed"}}.dump());
            return;
        }

'''

if old_late_mark in s:
    s = s.replace(old_late_mark, "", 1)
    print("patched: removed late token-used save")
elif "enrollment_token_mark_used_failed" not in s:
    print("unchanged: late token-used block already absent")
else:
    die("late token-used block still present but exact block did not match")

preconsume = '''        // Consume the setup/reset token before storing the OPAQUE credential.
        // This makes retry/replay fail closed if any later save returns an error.
        (*token_rec)["used_at"] = now;

        std::string serr;
        if (!routes_v5_save_opaque_enrollments_no_lock(enrollments_path, doc, &serr)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_finish", "deny", login, fingerprint, "enrollment_token_mark_used_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "enrollment_token_mark_used_failed"}}.dump());
            return;
        }

'''

backend_status_anchor = '''        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
'''
if preconsume not in s:
    if backend_status_anchor not in s:
        die("backend status anchor not found")
    s = s.replace(backend_status_anchor, preconsume + backend_status_anchor, 1)
    print("patched: consume token before credential save")
else:
    print("unchanged: pre-credential token consume already present")

p.write_text(s)
print("done")
