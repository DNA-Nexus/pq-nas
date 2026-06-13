#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("server/src/main.cpp")
if not p.exists():
    print(f"ERROR: missing {p}", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

def die(msg):
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

route_marker = 'srv.Post("/api/v4/admin/users/status"'
route_pos = s.find(route_marker)
if route_pos < 0:
    die("admin users status route not found")

helper_name = "invalidate_opaque_enrollment_tokens_for_fingerprint_on_status_revoke"

helper = r'''
    auto trim_ascii_for_opaque_enrollments = [](const std::string& in) -> std::string {
        std::size_t a = 0;
        while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a]))) ++a;

        std::size_t b = in.size();
        while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;

        return in.substr(a, b - a);
    };

    auto opaque_enrollments_path_for_admin_status = [&]() -> std::string {
        const char* raw = std::getenv("PQNAS_OPAQUE_ENROLLMENTS_PATH");
        const std::string env_path = trim_ascii_for_opaque_enrollments(raw ? raw : "");
        if (!env_path.empty()) return env_path;

        if (!users_path.empty()) {
            std::filesystem::path p(users_path);
            return (p.parent_path() / "opaque_enrollments.json").string();
        }

        return "/var/lib/pqnas/opaque_enrollments.json";
    };

    auto load_opaque_enrollments_for_admin_status = [](const std::string& path, std::string* err) -> json {
        if (err) err->clear();

        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            return json{{"version", 1}, {"tokens", json::array()}};
        }

        std::ifstream in(path);
        if (!in) {
            if (err) *err = "open_failed";
            return json{};
        }

        try {
            json doc = json::parse(in);
            if (!doc.is_object()) {
                if (err) *err = "json_not_object";
                return json{};
            }
            if (!doc.contains("tokens") || !doc["tokens"].is_array()) {
                doc["tokens"] = json::array();
            }
            if (!doc.contains("version")) {
                doc["version"] = 1;
            }
            return doc;
        } catch (const std::exception& e) {
            if (err) *err = std::string("json_parse_failed: ") + e.what();
            return json{};
        }
    };

    auto save_opaque_enrollments_for_admin_status = [](const std::string& path, const json& doc, std::string* err) -> bool {
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

            out << doc.dump(2) << "\n";
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
    };

    auto invalidate_opaque_enrollment_tokens_for_fingerprint_on_status_revoke =
        [&](const std::string& fp, std::size_t* invalidated, std::string* err) -> bool {
            if (invalidated) *invalidated = 0;
            if (err) err->clear();

            if (fp.empty()) {
                if (err) *err = "missing_fingerprint";
                return false;
            }

            const std::string path = opaque_enrollments_path_for_admin_status();
            const long now = static_cast<long>(std::time(nullptr));

            std::string lerr;
            json doc = load_opaque_enrollments_for_admin_status(path, &lerr);
            if (!lerr.empty()) {
                if (err) *err = "opaque_enrollments_load_failed: " + lerr;
                return false;
            }

            if (!doc.contains("tokens") || !doc["tokens"].is_array()) {
                doc["tokens"] = json::array();
            }

            std::size_t changed = 0;

            for (auto& rec : doc["tokens"]) {
                if (!rec.is_object()) continue;
                if (rec.value("fingerprint", "") != fp) continue;

                const long used_at = rec.value("used_at", 0L);
                const long expires_at = rec.value("expires_at", 0L);
                if (used_at > 0 || expires_at <= now) continue;

                rec["used_at"] = now;
                rec["invalidated_at"] = now;
                rec["invalidated_reason"] = "user_revoked";
                ++changed;
            }

            if (changed > 0) {
                std::string serr;
                if (!save_opaque_enrollments_for_admin_status(path, doc, &serr)) {
                    if (err) *err = "opaque_enrollments_save_failed: " + serr;
                    return false;
                }
            }

            if (invalidated) *invalidated = changed;
            return true;
        };

'''

if helper_name not in s:
    s = s[:route_pos] + helper + "\n" + s[route_pos:]
    print("prepared: helper insertion")
else:
    print("unchanged: helper already present")

# Recalculate route position after helper insert.
route_pos = s.find(route_marker)
if route_pos < 0:
    die("admin users status route not found after helper insert")

next_route = s.find("// POST /api/v4/admin/users/storage", route_pos)
if next_route < 0:
    next_route = s.find("srv.Post(\"/api/v4/admin/users/storage\"", route_pos)
if next_route < 0:
    die("could not find end boundary after admin users status route")

route = s[route_pos:next_route]

if "opaque.enrollment_tokens_invalidate_on_user_revoke" not in route:
    set_status_sub = "users.set_status(fp, status);"
    rel = route.find(set_status_sub)
    if rel < 0:
        die("users.set_status(fp, status) not found inside admin users status route")

    line_start = route.rfind("\n", 0, rel) + 1
    if line_start <= 0:
        die("could not find set_status line start")

    insert = '''        if (status == "revoked") {
            std::size_t opaque_tokens_invalidated = 0;
            std::string opaque_token_invalidate_err;

            if (!invalidate_opaque_enrollment_tokens_for_fingerprint_on_status_revoke(
                    fp,
                    &opaque_tokens_invalidated,
                    &opaque_token_invalidate_err)) {
                pqnas::AuditEvent ev;
                ev.event = "opaque.enrollment_tokens_invalidate_on_user_revoke";
                ev.outcome = "fail";
                ev.f["fingerprint"] = fp;
                ev.f["reason"] = opaque_token_invalidate_err;
                ev.f["ts"] = now_iso_utc();
                ev.f["actor_fp"] = actor_fp;
                ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
                audit_append(ev);

                reply_json(res, 500, json({
                    {"ok", false},
                    {"error", "server_error"},
                    {"message", "opaque enrollment token invalidation failed"},
                    {"detail", opaque_token_invalidate_err}
                }).dump());
                return;
            }

            pqnas::AuditEvent ev;
            ev.event = "opaque.enrollment_tokens_invalidate_on_user_revoke";
            ev.outcome = "ok";
            ev.f["fingerprint"] = fp;
            ev.f["invalidated"] = std::to_string(opaque_tokens_invalidated);
            ev.f["ts"] = now_iso_utc();
            ev.f["actor_fp"] = actor_fp;
            ev.f["ip"] = req.remote_addr.empty() ? "?" : req.remote_addr;
            audit_append(ev);
        }

'''
    abs_pos = route_pos + line_start
    s = s[:abs_pos] + insert + s[abs_pos:]
    print("prepared: revoke invalidation call insertion")
else:
    print("unchanged: revoke invalidation call already present")

# Final sanity checks before write.
if helper_name not in s:
    die("sanity failed: helper missing")
if "opaque.enrollment_tokens_invalidate_on_user_revoke" not in s:
    die("sanity failed: revoke invalidation audit marker missing")
if s.count(helper_name) < 2:
    die("sanity failed: helper appears only once; call probably missing")

p.write_text(s)
print("done")
