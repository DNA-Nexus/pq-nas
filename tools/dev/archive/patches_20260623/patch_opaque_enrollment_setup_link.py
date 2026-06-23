#!/usr/bin/env python3
from pathlib import Path
import sys

routes = Path("server/src/routes_v5.cc")
if not routes.exists():
    print(f"ERROR: missing {routes}", file=sys.stderr)
    sys.exit(1)

s = routes.read_text()

if '#include <fstream>' not in s:
    s = s.replace('#include <filesystem>\n', '#include <filesystem>\n#include <fstream>\n#include <system_error>\n', 1)

helper_marker = "routes_v5_opaque_enrollments_path"
if helper_marker not in s:
    anchor = "\n\nstruct RoutesV5OpaqueLoginPending {"
    if anchor not in s:
        print("ERROR: helper insertion anchor not found", file=sys.stderr)
        sys.exit(1)

    helpers = r'''

static std::mutex& routes_v5_opaque_enrollments_file_mu() {
    static std::mutex mu;
    return mu;
}

static std::string routes_v5_opaque_enrollments_path(const RoutesV5Context& ctx) {
    const char* raw = std::getenv("PQNAS_OPAQUE_ENROLLMENTS_PATH");
    std::string env_path = routes_v5_trim_ascii_copy(raw ? raw : "");
    if (!env_path.empty()) return env_path;

    if (ctx.users_path && !ctx.users_path->empty()) {
        std::filesystem::path p(*ctx.users_path);
        return (p.parent_path() / "opaque_enrollments.json").string();
    }

    return "/var/lib/pqnas/opaque_enrollments.json";
}

static bool routes_v5_is_safe_enrollment_token(const std::string& s) {
    if (s.size() < 32 || s.size() > 256) return false;

    for (unsigned char c : s) {
        const bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~';
        if (!ok) return false;
    }

    return true;
}

static json routes_v5_empty_opaque_enrollments_doc() {
    return json{{"version", 1}, {"tokens", json::array()}};
}

static json routes_v5_load_opaque_enrollments_no_lock(const std::string& path, std::string* err) {
    if (err) err->clear();

    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return routes_v5_empty_opaque_enrollments_doc();
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
}

static bool routes_v5_save_opaque_enrollments_no_lock(const std::string& path,
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

    out << doc.dump(2) << "\n";
    if (!out) {
        if (err) *err = "write_failed";
        return false;
    }

    return true;
}

static void routes_v5_prune_opaque_enrollments_doc(json& doc, long now) {
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

static std::optional<pqnas::UserRec> routes_v5_find_user_by_login_local(const RoutesV5Context& ctx,
                                                                 const std::string& login,
                                                                 std::string* out_fp = nullptr) {
    if (out_fp) out_fp->clear();
    if (!ctx.users || login.empty()) return std::nullopt;

    const auto snap = ctx.users->snapshot();
    for (const auto& kv : snap) {
        const auto& u = kv.second;
        if (!u.email.empty() &&
            pqnas::OpaqueCredentials::normalize_login(u.email) == login) {
            if (out_fp) *out_fp = u.fingerprint;
            return u;
        }
    }

    return std::nullopt;
}
'''
    s = s.replace(anchor, "\n\n" + helpers + anchor, 1)
    print("inserted OPAQUE enrollment helper functions")
else:
    print("unchanged: OPAQUE enrollment helpers already present")

routes_marker = "enrollment-token/create"
if routes_marker not in s:
    anchor = '    // ---- POST /api/auth/opaque/login/start ----'
    if anchor not in s:
        print("ERROR: route insertion anchor not found", file=sys.stderr)
        sys.exit(1)

    new_routes = r'''
    // ---- POST /api/admin/auth/opaque/enrollment-token/create ----
    //
    // Admin-only OPAQUE setup/reset token creation.
    //
    // The returned token is shown once and only its SHA-256 hash is stored.
    // The setup page uses the token to run browser-side OPAQUE registration
    // without sending the plaintext password to the server.
    srv.Post("/api/admin/auth/opaque/enrollment-token/create", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_auth_disabled"}, {"mode", routes_v5_auth_mode()}}.dump());
            return;
        }

        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "auth_cookie_checker_not_configured"}}.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_token_create", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        if (routes_v5_has_forbidden_password_fallback_field(j)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_token_create", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));

        std::string purpose =
            routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "purpose")));
        if (purpose.empty()) purpose = "new_user";

        if (purpose != "new_user" && purpose != "reset_password") {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_purpose"}}.dump());
            return;
        }

        long ttl = 86400;
        if (j.contains("expires_in_seconds") && j["expires_in_seconds"].is_number_integer()) {
            ttl = j["expires_in_seconds"].get<long>();
        }
        if (ttl < 300) ttl = 300;
        if (ttl > 604800) ttl = 604800;

        const bool enable_user_on_finish = j.value("enable_user_on_finish", true);

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        std::optional<UserRec> user;
        if (!fingerprint.empty()) {
            user = ctx.users->get(fingerprint);
        } else {
            user = routes_v5_find_user_by_login_local(ctx, login, &fingerprint);
        }

        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_token_create", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_token_create", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        std::string token = ctx.random_b64url ? ctx.random_b64url(32) : std::string{};
        if (!routes_v5_is_safe_enrollment_token(token)) {
            token = routes_v5_random_hex_id_128() + routes_v5_random_hex_id_128();
        }
        if (!routes_v5_is_safe_enrollment_token(token)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "token_rng_failed"}}.dump());
            return;
        }

        const std::string token_hash = sha256_hex(token);
        const std::string path = routes_v5_opaque_enrollments_path(ctx);

        {
            std::lock_guard<std::mutex> lock(routes_v5_opaque_enrollments_file_mu());

            std::string lerr;
            json doc = routes_v5_load_opaque_enrollments_no_lock(path, &lerr);
            if (!lerr.empty()) {
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_enrollments_load_failed"}, {"detail", lerr}}.dump());
                return;
            }

            routes_v5_prune_opaque_enrollments_doc(doc, now);

            doc["tokens"].push_back(json{
                {"token_hash", token_hash},
                {"login", login},
                {"fingerprint", fingerprint},
                {"purpose", purpose},
                {"created_by_fp", actor_fp},
                {"user_status_at_issue", user->status},
                {"created_at", now},
                {"expires_at", now + ttl},
                {"used_at", 0},
                {"enable_user_on_finish", enable_user_on_finish}
            });

            std::string serr;
            if (!routes_v5_save_opaque_enrollments_no_lock(path, doc, &serr)) {
                routes_v5_audit_password(ctx, req, "opaque.enrollment_token_create", "deny", login, actor_fp, "enrollment_save_failed");
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_enrollments_save_failed"}, {"detail", serr}}.dump());
                return;
            }
        }

        const std::string setup_path = std::string("/static/opaque-enroll.html?token=") + token;

        std::string origin = req_header_or_empty(req, "Origin");
        if (origin.empty()) {
            const std::string host = req_header_or_empty(req, "Host");
            std::string proto = req_header_or_empty(req, "X-Forwarded-Proto");
            if (proto.empty()) proto = "https";
            if (!host.empty()) origin = proto + "://" + host;
        }

        const std::string setup_url = origin.empty() ? setup_path : (origin + setup_path);

        routes_v5_audit_password(ctx, req, "opaque.enrollment_token_create", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"purpose", purpose},
            {"expires_at", now + ttl},
            {"enable_user_on_finish", enable_user_on_finish},
            {"token_shown_once", true},
            {"token", token},
            {"setup_path", setup_path},
            {"setup_url", setup_url}
        }.dump());
    });

    // ---- POST /api/auth/opaque/enrollment/start ----
    //
    // Public token-protected OPAQUE registration start.
    srv.Post("/api/auth/opaque/enrollment/start", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_auth_disabled"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        if (routes_v5_has_forbidden_password_fallback_field(j)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_start", "deny", "", "", "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string token = routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "token"));
        const std::string registration_request_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "registration_request_b64"));

        if (!routes_v5_is_safe_enrollment_token(token)) {
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_or_expired_token"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(registration_request_b64, 8192)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_registration_request_b64"}}.dump());
            return;
        }

        const std::string token_hash = sha256_hex(token);
        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("opaque.enrollment_start.") + token_hash,
                ip_for_rate_limit,
                20,
                std::chrono::seconds(60))) {
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_enrollment_attempts"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        const std::string path = routes_v5_opaque_enrollments_path(ctx);

        std::string login;
        std::string fingerprint;
        long expires_at = 0;

        {
            std::lock_guard<std::mutex> lock(routes_v5_opaque_enrollments_file_mu());

            std::string lerr;
            json doc = routes_v5_load_opaque_enrollments_no_lock(path, &lerr);
            if (!lerr.empty()) {
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_enrollments_load_failed"}}.dump());
                return;
            }

            routes_v5_prune_opaque_enrollments_doc(doc, now);

            for (const auto& rec : doc["tokens"]) {
                if (!rec.is_object()) continue;
                if (rec.value("token_hash", "") != token_hash) continue;
                if (rec.value("used_at", 0L) > 0) break;
                if (rec.value("expires_at", 0L) <= now) break;

                login = rec.value("login", "");
                fingerprint = rec.value("fingerprint", "");
                expires_at = rec.value("expires_at", 0L);
                break;
            }
        }

        if (login.empty() || fingerprint.empty()) {
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_or_expired_token"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value() ||
            user->email.empty() ||
            pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_or_expired_token"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result =
            helper.register_start(status.server_setup_path, login, registration_request_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_register_start_failed"},
                {"message", helper_err}
            }.dump());
            return;
        }

        const std::string registration_response_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "registration_response_b64"));

        if (!routes_v5_is_safe_b64ish(registration_response_b64, 8192)) {
            reply_json(res, 502, json{{"ok", false}, {"error", "opaque_helper_invalid_registration_response"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.enrollment_start", "ok", login, fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"registration_response_b64", registration_response_b64},
            {"expires_at", expires_at}
        }.dump());
    });

    // ---- POST /api/auth/opaque/enrollment/finish ----
    //
    // Public token-protected OPAQUE registration finish.
    srv.Post("/api/auth/opaque/enrollment/finish", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_opaque_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "opaque_auth_disabled"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        if (routes_v5_has_forbidden_password_fallback_field(j)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_finish", "deny", "", "", "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string token = routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "token"));
        const std::string registration_upload_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "registration_upload_b64"));
        std::string opaque_suite =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_suite"));

        if (opaque_suite.empty()) {
            opaque_suite = "opaque-ke-4.1.0-pre.0:ristretto255:triple-dh:sha512:argon2";
        }

        if (!routes_v5_is_safe_enrollment_token(token)) {
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_or_expired_token"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(registration_upload_b64, 262144)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_registration_upload_b64"}}.dump());
            return;
        }

        if (opaque_suite.size() > 128 || routes_v5_has_control_chars(opaque_suite)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_suite"}}.dump());
            return;
        }

        const std::string token_hash = sha256_hex(token);
        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("opaque.enrollment_finish.") + token_hash,
                ip_for_rate_limit,
                20,
                std::chrono::seconds(60))) {
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_enrollment_attempts"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        const std::string enrollments_path = routes_v5_opaque_enrollments_path(ctx);

        std::lock_guard<std::mutex> lock(routes_v5_opaque_enrollments_file_mu());

        std::string lerr;
        json doc = routes_v5_load_opaque_enrollments_no_lock(enrollments_path, &lerr);
        if (!lerr.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_enrollments_load_failed"}}.dump());
            return;
        }

        routes_v5_prune_opaque_enrollments_doc(doc, now);

        json* token_rec = nullptr;
        for (auto& rec : doc["tokens"]) {
            if (!rec.is_object()) continue;
            if (rec.value("token_hash", "") != token_hash) continue;
            token_rec = &rec;
            break;
        }

        if (!token_rec ||
            token_rec->value("used_at", 0L) > 0 ||
            token_rec->value("expires_at", 0L) <= now) {
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_or_expired_token"}}.dump());
            return;
        }

        const std::string login = token_rec->value("login", "");
        const std::string fingerprint = token_rec->value("fingerprint", "");
        const std::string user_status_at_issue = token_rec->value("user_status_at_issue", "");
        const bool enable_user_on_finish = token_rec->value("enable_user_on_finish", true);

        if (login.empty() || fingerprint.empty()) {
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_or_expired_token"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value() ||
            user->email.empty() ||
            pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_or_expired_token"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result = helper.register_finish(registration_upload_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_register_finish_failed"},
                {"message", helper_err}
            }.dump());
            return;
        }

        const std::string opaque_password_file_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "opaque_password_file_b64"));

        if (!routes_v5_is_safe_b64ish(opaque_password_file_b64, 262144)) {
            reply_json(res, 502, json{{"ok", false}, {"error", "opaque_helper_invalid_password_file"}}.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        pqnas::OpaqueCredentialRec cred;
        cred.login = login;
        cred.fingerprint = fingerprint;
        cred.opaque_password_file_b64 = opaque_password_file_b64;
        cred.opaque_suite = opaque_suite;
        cred.enabled = true;
        cred.temporary = false;

        const auto existing = creds.get(login);
        cred.created_at = existing.has_value() ? existing->created_at : now_iso;
        cred.updated_at = now_iso;

        if (!creds.upsert(cred) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_finish", "deny", login, fingerprint, "opaque_credentials_save_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_save_failed"}}.dump());
            return;
        }

        std::string final_user_status = user->status;
        bool user_enabled_by_enrollment = false;

        if (enable_user_on_finish) {
            if (!ctx.users_path || ctx.users_path->empty()) {
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_path_not_configured"}}.dump());
                return;
            }

            pqnas::UserRec updated_user = *user;
            updated_user.status = "enabled";
            final_user_status = updated_user.status;
            user_enabled_by_enrollment = (user->status != "enabled");

            if (!ctx.users->upsert(updated_user) || !ctx.users->save(*ctx.users_path)) {
                routes_v5_audit_password(ctx, req, "opaque.enrollment_finish", "deny", login, fingerprint, "user_enable_failed");
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "user_enable_failed"}}.dump());
                return;
            }
        }

        (*token_rec)["used_at"] = now;

        std::string serr;
        if (!routes_v5_save_opaque_enrollments_no_lock(enrollments_path, doc, &serr)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_finish", "deny", login, fingerprint, "enrollment_token_mark_used_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "enrollment_token_mark_used_failed"}}.dump());
            return;
        }

        const bool ready_for_login = cred.enabled && final_user_status == "enabled";

        routes_v5_audit_password(ctx, req, "opaque.enrollment_finish", "ok", login, fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"opaque_suite", opaque_suite},
            {"enabled", cred.enabled},
            {"user_status", final_user_status},
            {"user_enabled_by_enrollment", user_enabled_by_enrollment},
            {"ready_for_login", ready_for_login}
        }.dump());
    });

'''
    s = s.replace(anchor, new_routes + "\n" + anchor, 1)
    print("inserted OPAQUE enrollment token/start/finish routes")
else:
    print("unchanged: OPAQUE enrollment routes already present")

routes.write_text(s)

static_dir = Path("server/src/static")
static_dir.mkdir(parents=True, exist_ok=True)
page = static_dir / "opaque-enroll.html"
page.write_text(r'''<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>DNA-Nexus OPAQUE setup</title>
  <style>
    :root { color-scheme: light dark; }
    body {
      margin: 0;
      min-height: 100vh;
      display: grid;
      place-items: center;
      font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      background: radial-gradient(circle at top, #172554, #020617 65%);
      color: #e5e7eb;
    }
    .card {
      width: min(92vw, 480px);
      border: 1px solid rgba(148,163,184,.35);
      border-radius: 22px;
      padding: 28px;
      background: rgba(15,23,42,.88);
      box-shadow: 0 24px 80px rgba(0,0,0,.42);
    }
    h1 { margin: 0 0 8px; font-size: 1.55rem; }
    p { color: #cbd5e1; line-height: 1.5; }
    label { display:block; margin: 18px 0 8px; font-weight: 650; }
    input {
      box-sizing: border-box;
      width: 100%;
      padding: 13px 14px;
      border: 1px solid rgba(148,163,184,.45);
      border-radius: 12px;
      background: rgba(15,23,42,.7);
      color: #f8fafc;
      font: inherit;
    }
    button {
      width: 100%;
      margin-top: 18px;
      padding: 13px 14px;
      border: 0;
      border-radius: 12px;
      background: #38bdf8;
      color: #082f49;
      font-weight: 800;
      cursor: pointer;
    }
    button:disabled { opacity: .55; cursor: not-allowed; }
    .status {
      min-height: 1.5em;
      margin-top: 16px;
      color: #bfdbfe;
      white-space: pre-wrap;
      word-break: break-word;
    }
    .error { color: #fecaca; }
    .ok { color: #bbf7d0; }
    .small { font-size: .9rem; color: #94a3b8; }
  </style>
</head>
<body>
  <main class="card">
    <h1>Set your DNA-Nexus password</h1>
    <p>This page uses OPAQUE zero-knowledge password setup. Your password is processed in this browser and is not sent to the server.</p>

    <form id="form">
      <label for="password">New password</label>
      <input id="password" type="password" autocomplete="new-password" minlength="12" required>

      <label for="confirm">Confirm password</label>
      <input id="confirm" type="password" autocomplete="new-password" minlength="12" required>

      <button id="submit" type="submit">Set password</button>
    </form>

    <div id="status" class="status"></div>
    <p class="small">The setup link is single-use. If it has expired, ask an administrator for a new link.</p>
  </main>

  <script type="module">
    const OPAQUE_CLIENT_MODULE_URL = "/static/opaque/pqnas_opaque_browser_client.js?v=20260613-opaque-enroll-1";
    const OPAQUE_CLIENT_WASM_URL = "/static/opaque/pqnas_opaque_browser_client_bg.wasm?v=20260613-opaque-enroll-1";

    const params = new URLSearchParams(location.search);
    const token = (params.get("token") || "").trim();

    const form = document.getElementById("form");
    const submit = document.getElementById("submit");
    const statusEl = document.getElementById("status");
    const passwordEl = document.getElementById("password");
    const confirmEl = document.getElementById("confirm");

    function setStatus(message, cls = "") {
      statusEl.className = "status " + cls;
      statusEl.textContent = message;
    }

    function parseOpaqueResult(value) {
      if (typeof value === "string") return JSON.parse(value);
      if (value && typeof value === "object") return value;
      throw new Error("Unexpected OPAQUE client result");
    }

    async function loadOpaqueClient() {
      const mod = await import(OPAQUE_CLIENT_MODULE_URL);
      if (typeof mod.default === "function") {
        await mod.default(OPAQUE_CLIENT_WASM_URL);
      }
      if (typeof mod.opaqueRegistrationStart !== "function" ||
          typeof mod.opaqueRegistrationFinish !== "function") {
        throw new Error("OPAQUE registration functions missing");
      }
      return mod;
    }

    async function postJson(path, body) {
      const res = await fetch(path, {
        method: "POST",
        credentials: "same-origin",
        headers: { "Content-Type": "application/json" },
        cache: "no-store",
        body: JSON.stringify(body)
      });
      let data = null;
      try { data = await res.json(); } catch (_) {}
      if (!res.ok || !data || data.ok !== true) {
        const msg = data && (data.message || data.error) ? (data.message || data.error) : `HTTP ${res.status}`;
        throw new Error(msg);
      }
      return data;
    }

    if (!token) {
      submit.disabled = true;
      setStatus("Missing setup token.", "error");
    }

    form.addEventListener("submit", async (ev) => {
      ev.preventDefault();

      const password = passwordEl.value;
      const confirm = confirmEl.value;

      if (password.length < 12) {
        setStatus("Password must be at least 12 characters.", "error");
        return;
      }
      if (password !== confirm) {
        setStatus("Passwords do not match.", "error");
        return;
      }

      submit.disabled = true;

      try {
        setStatus("Loading OPAQUE client...");
        const opaque = await loadOpaqueClient();

        setStatus("Creating registration request in browser...");
        const regStart = parseOpaqueResult(await opaque.opaqueRegistrationStart(password));
        if (!regStart.ok || !regStart.client_registration_state_b64 || !regStart.registration_request_b64) {
          throw new Error("OPAQUE registration start failed");
        }

        setStatus("Contacting server...");
        const serverStart = await postJson("/api/auth/opaque/enrollment/start", {
          token,
          registration_request_b64: regStart.registration_request_b64
        });

        setStatus("Finalizing registration in browser...");
        const regFinish = parseOpaqueResult(await opaque.opaqueRegistrationFinish(
          password,
          regStart.client_registration_state_b64,
          serverStart.registration_response_b64
        ));

        if (!regFinish.ok || !regFinish.registration_upload_b64) {
          throw new Error("OPAQUE registration finish failed");
        }

        setStatus("Saving credential...");
        const done = await postJson("/api/auth/opaque/enrollment/finish", {
          token,
          registration_upload_b64: regFinish.registration_upload_b64
        });

        passwordEl.value = "";
        confirmEl.value = "";

        if (done.ready_for_login) {
          setStatus(`Password set. You can now sign in as ${done.login}.`, "ok");
        } else {
          setStatus("Password set, but the account is not enabled yet. Ask an administrator to enable it.", "ok");
        }
      } catch (err) {
        setStatus(`Setup failed: ${err && err.message ? err.message : String(err)}`, "error");
        submit.disabled = false;
      }
    });
  </script>
</body>
</html>
''')
print(f"wrote {page}")
print("done")
