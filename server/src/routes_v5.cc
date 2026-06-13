// server/src/routes_v5.cc
// routes_v5.cc
//
// v5 Stateless Login ("2A" flow) HTTP routes.
//
// Design goals
// - Stateless-ready correlation: clients can use `k = H(st)` (st_hash_b64) as the
//   primary lookup key. This avoids reliance on `sid` for v5.
// - Separation of concerns: this file is *transport + orchestration only*.
//   All crypto, token signing, storage, and auditing are delegated to callbacks
//   in RoutesV5Context (dependency injection).
// - Short-lived server-side state: the server keeps only minimal "pending" +
//   "approval" entries for UX and single-use consumption. These are pruned
//   aggressively by TTL to keep memory bounded.
//
// Flow summary
//   1) /api/v5/session
//      - Mint request token `st` (signed), return {sid, st, k, iat, exp, qr_svg}
//      - Insert PendingEntry keyed by `k` so status can immediately report "pending"
//   2) /api/v5/qr.svg
//      - Render QR containing dna://auth?v=5&st=...&origin=...&app=...
//   3) /api/v5/status (GET or POST)
//      - Resolve correlation key from {st|k|sid}
//      - Report {approved|pending|missing} with TTL metadata
//   4) /api/v5/consume
//      - Resolve correlation key from {st|k|sid}
//      - If approved, issue Set-Cookie(pqnas_session=...) and atomically consume
//        approval + pending entries (one-time use).
//
// Security notes
// - `st` is a signed request token. `k` is derived from st and is safe to expose.
// - `/consume` is intentionally strict: if approval exists but cookie is missing,
//   we fail loudly (500) because a partially-approved login is a server bug.
// - All JSON responses are no-store to avoid caching sensitive flow state.

#include "routes_v5.h"
#include "users_registry.h"
#include "password_credentials.h"
#include "dna_identity_generator.h"
#include "opaque_backend_status.h"
#include "opaque_credentials.h"
#include "opaque_helper_client.h"
#include <openssl/sha.h>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <sodium.h>

#include <algorithm>
#include <chrono>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

using nlohmann::json;


// Uniform JSON response helper:
// - forces application/json + no-store
// - caller provides already-serialized JSON string (avoids double-encoding)
static void reply_json(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_header("Content-Type", "application/json; charset=utf-8");
    res.set_header("Cache-Control", "no-store");
    res.body = body;
}


static std::string req_header_or_empty(const httplib::Request& req, const char* key) {
    auto it = req.headers.find(key);
    return (it == req.headers.end()) ? std::string{} : it->second;
}

static std::string audit_safe_header_value(const std::string& raw, std::size_t max_len = 512) {
    std::string out;
    out.reserve(std::min(raw.size(), max_len));

    for (unsigned char c : raw) {
        if (out.size() >= max_len) break;

        if (c < 0x20 || c == 0x7f) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }

    return out;
}

static bool routes_v5_simple_ip_rate_limit_allow(
    const std::string& bucket,
    const std::string& ip,
    int max_hits,
    std::chrono::seconds window
) {
    using Clock = std::chrono::steady_clock;

    static std::mutex mu;
    static std::unordered_map<std::string, std::deque<Clock::time_point>> hits;

    const auto now = Clock::now();
    const auto cutoff = now - window;
    const std::string key = bucket + "\n" + (ip.empty() ? "?" : ip);

    std::lock_guard<std::mutex> lock(mu);

    auto& q = hits[key];
    while (!q.empty() && q.front() < cutoff) {
        q.pop_front();
    }

    if ((int)q.size() >= max_hits) {
        return false;
    }

    q.push_back(now);

    if (hits.size() > 8192) {
        for (auto it = hits.begin(); it != hits.end(); ) {
            auto& v = it->second;
            while (!v.empty() && v.front() < cutoff) {
                v.pop_front();
            }
            if (v.empty()) {
                it = hits.erase(it);
            } else {
                ++it;
            }
        }
    }

    return true;
}

static std::string routes_v5_trim_ascii_copy(const std::string& s);

static std::string routes_v5_opaque_credentials_path() {
    const char* env = std::getenv("PQNAS_OPAQUE_CREDENTIALS_PATH");
    const std::string env_path = routes_v5_trim_ascii_copy(env ? env : "");
    if (!env_path.empty()) {
        return env_path;
    }

    const char* cfg_env = std::getenv("PQNAS_CONFIG");
    const std::string cfg_path = routes_v5_trim_ascii_copy(cfg_env ? cfg_env : "");
    if (!cfg_path.empty()) {
        return (std::filesystem::path(cfg_path) / "opaque_credentials.json").string();
    }

    const char* cfg_root_env = std::getenv("PQNAS_CONFIG_ROOT");
    const std::string cfg_root_path = routes_v5_trim_ascii_copy(cfg_root_env ? cfg_root_env : "");
    if (!cfg_root_path.empty()) {
        return (std::filesystem::path(cfg_root_path) / "opaque_credentials.json").string();
    }

    return "/etc/pqnas/opaque_credentials.json";
}

static bool routes_v5_is_safe_b64ish(const std::string& s, std::size_t max_len) {
    if (s.empty() || s.size() > max_len) return false;

    for (unsigned char c : s) {
        const bool ok =
            (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
            c == '+' ||
            c == '/' ||
            c == '=' ||
            c == '-' ||
            c == '_' ||
            c == '.';

        if (!ok) return false;
    }

    return true;
}

static std::string routes_v5_request_ip(
    const RoutesV5Context& ctx,
    const httplib::Request& req
) {
    if (ctx.client_ip) {
        const std::string ip = ctx.client_ip(req);
        if (!ip.empty()) return ip;
    }

    return req.remote_addr.empty() ? "?" : req.remote_addr;
}

static void routes_v5_secure_clear_string(std::string& s) {
    if (s.empty()) {
        return;
    }

    sodium_memzero(s.data(), s.size());
    s.clear();
    s.shrink_to_fit();
}

static bool routes_v5_append_json_member_to_object(std::string& object_json,
                                                   const std::string& member_json) {
    if (object_json.empty() || object_json.back() != '}') {
        return false;
    }

    object_json.pop_back();

    if (object_json.size() > 1) {
        object_json.push_back(',');
    }

    object_json += member_json;
    object_json.push_back('}');
    return true;
}

// Normalizes base64 values that arrive via URL/query decoding.
// Some stacks decode '+' as space in query parameters (application/x-www-form-urlencoded).
// We reverse that and trim surrounding whitespace to keep k parsing robust.
static std::string normalize_query_b64(std::string s) {
    // In URL query params, '+' often becomes ' ' after form-style decoding.
    // Our k is standard base64, so undo that when it happens.
    for (char& ch : s) {
        if (ch == ' ') ch = '+';
    }
    // also trim accidental surrounding whitespace
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\n' || s.front() == '\r')) s.erase(s.begin());
    while (!s.empty() && (s.back()  == ' ' || s.back()  == '\t' || s.back()  == '\n' || s.back()  == '\r')) s.pop_back();
    return s;
}

static std::string sha256_hex(const std::string& s) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(s.data()), s.size(), digest);

    static const char hex[] = "0123456789abcdef";
    std::string out;
    out.resize(SHA256_DIGEST_LENGTH * 2);

    for (size_t i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        out[i * 2]     = hex[(digest[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[digest[i] & 0x0f];
    }
    return out;
}

static std::string get_cookie_value(const httplib::Request& req, const std::string& name) {
    auto it = req.headers.find("Cookie");
    if (it == req.headers.end()) return {};

    const std::string& cookies = it->second;
    size_t pos = 0;

    while (pos < cookies.size()) {
        while (pos < cookies.size() && (cookies[pos] == ' ' || cookies[pos] == ';')) pos++;

        size_t eq = cookies.find('=', pos);
        if (eq == std::string::npos) break;

        size_t end = cookies.find(';', eq + 1);

        std::string key = cookies.substr(pos, eq - pos);
        std::string val = (end == std::string::npos)
            ? cookies.substr(eq + 1)
            : cookies.substr(eq + 1, end - (eq + 1));

        if (key == name) return val;

        if (end == std::string::npos) break;
        pos = end + 1;
    }

    return {};
}

static std::string make_preauth_cookie(const std::string& value, long max_age_sec) {
    return std::string("pqnas_preauth=") + value +
           "; Path=/api/v5/consume" +
           "; Max-Age=" + std::to_string(max_age_sec) +
           "; HttpOnly" +
           "; SameSite=Strict" +
           "; Secure";
}

static std::string clear_preauth_cookie() {
    return std::string("pqnas_preauth=") +
           "; Path=/api/v5/consume" +
           "; Max-Age=0" +
           "; HttpOnly" +
           "; SameSite=Strict" +
           "; Secure";
}

// Strictly parse request body as a JSON object. Returns false with a stable
// machine-readable error string for consistent 400 responses.
static bool parse_json_body(const httplib::Request& req, json& out, std::string& err) {
    try {
        if (req.body.empty()) { err = "empty_body"; return false; }
        out = json::parse(req.body);
        if (!out.is_object()) { err = "json_must_be_object"; return false; }
        return true;
    } catch (const std::exception& e) {
        err = std::string("json_parse_error: ") + e.what();
        return false;
    }
}

// Extract a correlation key from a JSON object:
// - If "k" is present, use it directly (normalized base64).
// - Else if "st" is present, derive k = H(st) via ctx.st_hash_b64_from_st.
// This supports "2A" POST /status style polling where the browser only has `st`.
// Returns false with a stable error code if fields are missing/invalid.
static bool get_key_from_json(const RoutesV5Context& ctx,
                              const json& j,
                              std::string& out_key,
                              std::string& err) {
    // Prefer explicit k if provided
    if (j.contains("k") && j["k"].is_string()) {
        out_key = normalize_query_b64(j["k"].get<std::string>());
        if (out_key.empty()) { err = "k_empty"; return false; }
        return true;
    }

    // Or derive from st
    if (j.contains("st") && j["st"].is_string()) {
        const std::string st = j["st"].get<std::string>();
        if (st.empty()) { err = "st_empty"; return false; }
        if (!ctx.st_hash_b64_from_st) { err = "server_missing_st_hash"; return false; }
        out_key = ctx.st_hash_b64_from_st(st);
        if (out_key.empty()) { err = "k_derive_failed"; return false; }
        return true;
    }

    err = "missing k/st";
    return false;
}

// resolve_approval_key_from_req
// Resolve the lookup key used by /status and /consume.
// Inputs can arrive via query (GET) or body (POST). Body fields override query.
// Accepted fields (in priority order):
//   1) st  -> derive k = H(st) (preferred, stateless-ready)
//   2) k   -> direct correlation key (normalized base64)
//   3) sid -> legacy/debug correlation key
//
// Rationale
// - v5 prefers `k` derived from signed `st` to avoid server-issued session IDs.
// - sid remains accepted for debugging and transitional compatibility.
static bool resolve_approval_key_from_req(const RoutesV5Context& ctx,
                                         const httplib::Request& req,
                                         const json* body_opt,
                                         std::string& out_key,
                                         std::string& err) {
	auto get_param = [&](const char* name) -> std::string {
    	auto it = req.params.find(name);
    	if (it == req.params.end()) return "";
    	std::string v = it->second;
    	if (std::string(name) == "k") v = normalize_query_b64(std::move(v));
    	return v;
	};

	std::string st;
	std::string k   = normalize_query_b64(get_param("k"));
	std::string sid = get_param("sid");

    // body fields override query (for POST)
    if (body_opt && body_opt->is_object()) {
        if (body_opt->contains("st") && (*body_opt)["st"].is_string())   st  = (*body_opt)["st"].get<std::string>();
        if (body_opt->contains("k")  && (*body_opt)["k"].is_string())    k   = normalize_query_b64((*body_opt)["k"].get<std::string>());
        if (body_opt->contains("sid")&& (*body_opt)["sid"].is_string())  sid = (*body_opt)["sid"].get<std::string>();
    }

    if (!st.empty()) {
        if (!ctx.st_hash_b64_from_st) { err = "server_missing_st_hash"; return false; }
        out_key = ctx.st_hash_b64_from_st(st);
        if (out_key.empty()) { err = "k_derive_failed"; return false; }
        return true;
    }

    if (!k.empty())   { out_key = k;   return true; }
    if (!sid.empty()) { out_key = sid; return true; }

    err = "missing st/k/sid";
    return false;
}


static std::string v5_json_string_or_empty(const nlohmann::json& j, const char* key) {
    if (!key) return std::string{};

    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) {
        return std::string{};
    }

    return it->get<std::string>();
}

static std::string app_pair_trim_ascii_space_copy(const std::string& in) {
    std::size_t a = 0;
    while (a < in.size()) {
        const unsigned char c = static_cast<unsigned char>(in[a]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++a;
    }

    std::size_t b = in.size();
    while (b > a) {
        const unsigned char c = static_cast<unsigned char>(in[b - 1]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        --b;
    }

    return in.substr(a, b - a);
}

static bool app_pair_has_control_chars(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

static bool sanitize_app_pair_text_field(const char* field,
                                         const std::string& raw,
                                         std::size_t max_bytes,
                                         std::string& out,
                                         std::string& err) {
    out = app_pair_trim_ascii_space_copy(raw);

    if (out.size() > max_bytes) {
        err = std::string(field ? field : "field") + " too long";
        out.clear();
        return false;
    }

    if (app_pair_has_control_chars(out)) {
        err = std::string(field ? field : "field") + " contains control characters";
        out.clear();
        return false;
    }

    return true;
}

static bool sanitize_app_pair_platform(const std::string& raw,
                                       std::string& out,
                                       std::string& err) {
    if (!sanitize_app_pair_text_field("platform", raw, 24, out, err)) {
        return false;
    }

    if (out.empty()) {
        out = "android";
        return true;
    }

    for (unsigned char c : out) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_' ||
            c == '.';

        if (!ok) {
            err = "platform contains invalid characters";
            out.clear();
            return false;
        }
    }

    return true;
}

static bool sanitize_app_pair_token(const std::string& raw,
                                    std::string& out,
                                    std::string& err) {
    out = app_pair_trim_ascii_space_copy(raw);

    if (out.empty()) {
        err = "missing pair_token";
        return false;
    }

    if (out.size() > 128) {
        err = "pair_token too long";
        out.clear();
        return false;
    }

    for (unsigned char c : out) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '-' ||
            c == '_';

        if (!ok) {
            err = "invalid pair_token";
            out.clear();
            return false;
        }
    }

    return true;
}


static void audit_v5_req(const RoutesV5Context& ctx,
                         const std::string& event,
                         const std::string& outcome,
                         const httplib::Request& req,
                         const std::function<void(std::map<std::string,std::string>&)>& fill) {
    if (!ctx.audit_emit) return;

    ctx.audit_emit(event, outcome, [&](std::map<std::string,std::string>& f) {
        const std::string ip = ctx.client_ip ? ctx.client_ip(req) : req.remote_addr;
        if (!ip.empty()) f["ip"] = ip;

        const std::string ua = audit_safe_header_value(req_header_or_empty(req, "User-Agent"));
        if (!ua.empty()) f["ua"] = ua;

        const std::string xff = audit_safe_header_value(req_header_or_empty(req, "X-Forwarded-For"));
        if (!xff.empty()) f["xff"] = xff;

        const std::string cf_ip = audit_safe_header_value(req_header_or_empty(req, "CF-Connecting-IP"));
        if (!cf_ip.empty()) f["cf_ip"] = cf_ip;

        if (fill) fill(f);
    });
}


static bool pending_admin_user_is_enabled(const RoutesV5Context& ctx,
                                          const RoutesV5Context::PendingEntry& pe) {
    if (pe.reason != "pending_admin") return false;
    if (pe.fingerprint_hex.empty()) return false;
    if (!ctx.users) return false;
    return ctx.users->is_enabled_user(pe.fingerprint_hex);
}

static bool mint_approval_for_pending_admin(const RoutesV5Context& ctx,
                                            const std::string& key,
                                            const RoutesV5Context::PendingEntry& pe,
                                            long now,
                                            RoutesV5Context::ApprovalEntry& out,
                                            std::string& err) {
    if (!ctx.approvals_put) {
        err = "approvals_put_not_configured";
        return false;
    }

    if (!ctx.b64_std || !ctx.session_cookie_mint || !ctx.cookie_key) {
        err = "cookie_mint_not_configured";
        return false;
    }

    const long sess_iat = now;
    const long sess_exp = now + (ctx.sess_ttl ? *ctx.sess_ttl : 3600);

    const std::string fp_b64 = ctx.b64_std(
        reinterpret_cast<const unsigned char*>(pe.fingerprint_hex.data()),
        pe.fingerprint_hex.size()
    );

    std::string cookie_val;
    if (!ctx.session_cookie_mint(ctx.cookie_key, fp_b64, sess_iat, sess_exp, cookie_val) ||
        cookie_val.empty()) {
        err = "cookie_mint_failed";
        return false;
    }

    RoutesV5Context::ApprovalEntry ae;
    ae.cookie_val = cookie_val;
    ae.fingerprint = pe.fingerprint_hex;
    ae.expires_at = now + 120;

    ctx.approvals_put(key, ae);
    out = ae;

    if (ctx.audit_emit) {
        ctx.audit_emit("v5.pending_admin_promoted", "ok",
                       [&](std::map<std::string,std::string>& f) {
            f["k"] = key;
            f["fingerprint"] = pe.fingerprint_hex;
            f["approval_expires_at"] = std::to_string(ae.expires_at);
        });
    }

    return true;
}

static void reply_pending_or_promoted_status(const RoutesV5Context& ctx,
                                             httplib::Response& res,
                                             const std::string& key,
                                             const RoutesV5Context::PendingEntry& pe,
                                             long now) {
    if (pending_admin_user_is_enabled(ctx, pe)) {
        RoutesV5Context::ApprovalEntry ae;
        std::string err;

        if (!mint_approval_for_pending_admin(ctx, key, pe, now, ae, err)) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", err},
                {"k", key}
            }.dump());
            return;
        }

        reply_json(res, 200, json{
            {"ok", true},
            {"approved", true},
            {"state", "approved"},
            {"k", key},
            {"expires_at", ae.expires_at}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"approved", false},
        {"pending", true},
        {"state", "pending"},
        {"k", key},
        {"expires_at", pe.expires_at},
        {"reason", pe.reason}
    }.dump());
}


static std::string routes_v5_trim_ascii_copy(const std::string& s) {
    std::size_t a = 0;
    while (a < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[a]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        ++a;
    }

    std::size_t b = s.size();
    while (b > a) {
        const unsigned char c = static_cast<unsigned char>(s[b - 1]);
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        --b;
    }

    return s.substr(a, b - a);
}

static std::string routes_v5_lower_ascii_copy(std::string s) {
    for (char& ch : s) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
    }
    return s;
}

static std::string routes_v5_auth_mode() {
    // Login UI/auth selection is separate from the older PQNAS_AUTH_MODE=v5
    // server auth-stack selector. Keep PQNAS_AUTH_MODE=v5 intact and use
    // PQNAS_LOGIN_MODE=password for the new password-only login mode.
    const char* login_raw = std::getenv("PQNAS_LOGIN_MODE");
    std::string login_mode = routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(login_raw ? login_raw : ""));

    if (login_mode == "password") return "password";
    if (login_mode == "opaque") return "opaque";
    if (login_mode == "qr") return "qr";

    // Backward-compatible emergency override only. Existing deployments often
    // have PQNAS_AUTH_MODE=v5, which must continue to mean QR/v5 auth stack.
    const char* raw = std::getenv("PQNAS_AUTH_MODE");
    std::string mode = routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(raw ? raw : ""));

    if (mode == "password") return "password";
    if (mode == "opaque") return "opaque";
    return "qr";
}

static bool routes_v5_qr_auth_enabled() {
    return routes_v5_auth_mode() == "qr";
}


static std::string routes_v5_normalize_recovery_words(std::string s) {
    std::string out;
    out.reserve(s.size());

    bool in_space = true;

    for (unsigned char uch : s) {
        char ch = static_cast<char>(uch);

        if (std::isspace(uch)) {
            if (!in_space) {
                out.push_back(' ');
                in_space = true;
            }
            continue;
        }

        if (uch < 0x20 || uch == 0x7f) {
            continue;
        }

        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }

        out.push_back(ch);
        in_space = false;
    }

    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }

    return out;
}

static bool routes_v5_password_auth_enabled() {
    return routes_v5_auth_mode() == "password";
}

static bool routes_v5_opaque_auth_enabled() {
    return routes_v5_auth_mode() == "opaque";
}

static bool routes_v5_has_control_chars(const std::string& s) {
    for (unsigned char c : s) {
        if (c < 0x20 || c == 0x7f) return true;
    }
    return false;
}

static std::string routes_v5_password_credentials_path(const RoutesV5Context& ctx) {
    const char* raw = std::getenv("PQNAS_PASSWORD_CREDENTIALS_PATH");
    std::string env_path = routes_v5_trim_ascii_copy(raw ? raw : "");
    if (!env_path.empty()) return env_path;

    if (ctx.users_path && !ctx.users_path->empty()) {
        std::filesystem::path p(*ctx.users_path);
        return (p.parent_path() / "password_credentials.json").string();
    }

    return "/var/lib/pqnas/password_credentials.json";
}

static std::string routes_v5_b64std_from_string(const std::string& s) {
    if (sodium_init() < 0) return {};

    const std::size_t out_len =
        sodium_base64_ENCODED_LEN(s.size(), sodium_base64_VARIANT_ORIGINAL);

    std::string out(out_len, '\0');

    char* encoded = sodium_bin2base64(out.data(),
                                      out.size(),
                                      reinterpret_cast<const unsigned char*>(s.data()),
                                      s.size(),
                                      sodium_base64_VARIANT_ORIGINAL);
    if (!encoded) return {};

    while (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }

    return out;
}

static std::string routes_v5_bootstrap_token_from_req(const httplib::Request& req,
                                                      const nlohmann::json& j) {
    auto it = req.headers.find("X-PQNAS-Bootstrap-Token");
    if (it != req.headers.end()) {
        const std::string v = routes_v5_trim_ascii_copy(it->second);
        if (!v.empty()) return v;
    }

    if (j.is_object() && j.contains("bootstrap_token") && j["bootstrap_token"].is_string()) {
        return routes_v5_trim_ascii_copy(j["bootstrap_token"].get<std::string>());
    }

    return {};
}

static void routes_v5_audit_password(const RoutesV5Context& ctx,
                                     const httplib::Request& req,
                                     const std::string& event,
                                     const std::string& outcome,
                                     const std::string& login,
                                     const std::string& fingerprint,
                                     const std::string& reason) {
    if (!ctx.audit_emit) return;

    ctx.audit_emit(event, outcome, [&](std::map<std::string,std::string>& f) {
        const std::string ip = ctx.client_ip ? ctx.client_ip(req) : req.remote_addr;
        if (!ip.empty()) f["ip"] = ip;
        if (!login.empty()) f["login"] = login;
        if (!fingerprint.empty()) f["fingerprint"] = fingerprint;
        if (!reason.empty()) f["reason"] = reason;
    });
}

static bool routes_v5_opaque_backend_ready_for_registration(
    const pqnas::OpaqueBackendStatus& status) {
    return status.credentials_file_exists &&
           status.credentials_file_readable &&
           status.credentials_store_valid &&
           status.server_setup_file_exists &&
           status.server_setup_file_readable &&
           status.server_setup_valid &&
           status.helper_exists &&
           status.helper_executable &&
           status.helper_version_ok &&
           status.helper_self_test_ok;
}

static bool routes_v5_has_forbidden_password_fallback_field(const nlohmann::json& j) {
    return j.contains("password") ||
           j.contains("plaintext_password") ||
           j.contains("password_hash") ||
           j.contains("classic_password_hash") ||
           j.contains("argon2id_hash");
}

static bool routes_v5_helper_result_json(
    const pqnas::OpaqueHelperClientResult& result,
    nlohmann::json& out,
    std::string& err) {
    if (!result.ok) {
        err = result.error.empty() ? "opaque_helper_failed" : result.error;
        return false;
    }

    try {
        out = nlohmann::json::parse(result.output);
    } catch (const std::exception& e) {
        err = std::string("opaque_helper_json_parse_failed: ") + e.what();
        return false;
    }

    if (!out.is_object()) {
        err = "opaque_helper_json_not_object";
        return false;
    }

    if (!out.value("ok", false)) {
        err = out.value("error", "opaque_helper_reported_failure");
        return false;
    }

    return true;
}


struct RoutesV5OpaqueLoginPending {
    std::string login;
    std::string fingerprint;
    std::string server_login_state_b64;
    long expires_at = 0;
};

static std::mutex& routes_v5_opaque_login_pending_mu() {
    static std::mutex mu;
    return mu;
}

static std::unordered_map<std::string, RoutesV5OpaqueLoginPending>&
routes_v5_opaque_login_pending_map() {
    static std::unordered_map<std::string, RoutesV5OpaqueLoginPending> m;
    return m;
}

static long routes_v5_now_epoch_safe(const RoutesV5Context& ctx) {
    if (ctx.now_epoch) {
        return ctx.now_epoch();
    }

    return static_cast<long>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count()
    );
}

static void routes_v5_opaque_login_pending_prune(long now) {
    std::lock_guard<std::mutex> lock(routes_v5_opaque_login_pending_mu());
    auto& m = routes_v5_opaque_login_pending_map();

    for (auto it = m.begin(); it != m.end();) {
        if (it->second.expires_at <= now) {
            it = m.erase(it);
        } else {
            ++it;
        }
    }
}

static std::string routes_v5_random_hex_id_128() {
    if (sodium_init() < 0) return {};

    unsigned char buf[16];
    randombytes_buf(buf, sizeof(buf));

    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (unsigned char b : buf) {
        oss << std::setw(2) << static_cast<unsigned int>(b);
    }
    return oss.str();
}

static bool routes_v5_is_safe_hex_id_128(const std::string& s) {
    if (s.size() != 32) return false;

    for (char ch : s) {
        const bool ok =
            (ch >= '0' && ch <= '9') ||
            (ch >= 'a' && ch <= 'f') ||
            (ch >= 'A' && ch <= 'F');

        if (!ok) return false;
    }

    return true;
}

static bool routes_v5_opaque_login_pending_put(
    const std::string& opaque_login_id,
    const RoutesV5OpaqueLoginPending& pending) {
    if (!routes_v5_is_safe_hex_id_128(opaque_login_id) ||
        pending.login.empty() ||
        pending.fingerprint.empty() ||
        pending.server_login_state_b64.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(routes_v5_opaque_login_pending_mu());
    auto& m = routes_v5_opaque_login_pending_map();

    if (m.size() > 4096) {
        return false;
    }

    m[opaque_login_id] = pending;
    return true;
}

static bool routes_v5_opaque_login_pending_pop(
    const std::string& opaque_login_id,
    long now,
    RoutesV5OpaqueLoginPending& out) {
    if (!routes_v5_is_safe_hex_id_128(opaque_login_id)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(routes_v5_opaque_login_pending_mu());
    auto& m = routes_v5_opaque_login_pending_map();

    auto it = m.find(opaque_login_id);
    if (it == m.end()) {
        return false;
    }

    out = it->second;
    m.erase(it);

    if (out.expires_at <= now) {
        return false;
    }

    return true;
}



void register_routes_v5(httplib::Server& srv, const RoutesV5Context& ctx) {

    // ---- GET /api/auth/config ----
    srv.Get("/api/auth/config", [&](const httplib::Request&, httplib::Response& res) {
        const std::string mode = routes_v5_auth_mode();
        reply_json(res, 200, json{
            {"ok", true},
            {"mode", mode},
            {"qr_enabled", mode == "qr"},
            {"password_enabled", mode == "password"},
            {"opaque_enabled", mode == "opaque"},
            {"password_scheme", mode == "opaque" ? "opaque" : (mode == "password" ? "argon2id" : "")}
        }.dump());
    });

    // ---- GET /api/admin/auth/opaque/status ----
    //
    // Admin-only internal diagnostic endpoint.
    //
    // This intentionally exposes backend readiness details only to admins.
    // Public OPAQUE login endpoints must continue returning generic fail-closed
    // errors so callers cannot enumerate backend state or user existence.
    srv.Get("/api/admin/auth/opaque/status", [&](const httplib::Request& req, httplib::Response& res) {
        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "auth_cookie_checker_not_configured"}
            }.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.admin_status", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        std::string body = pqnas::opaque_backend_internal_diagnostic_json(status);

        if (!routes_v5_append_json_member_to_object(body, "\"ok\":true")) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "opaque_status_response_build_failed"}
            }.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.admin_status", "ok", "", actor_fp, "");
        reply_json(res, 200, body);
    });


    // ---- POST /api/admin/auth/opaque/registration/start ----
    //
    // Admin-only OPAQUE registration start.
    //
    // This runs the server side of OPAQUE registration start through the helper.
    // It does not write credentials and does not enable login.
    srv.Post("/api/admin/auth/opaque/registration/start", [&](const httplib::Request& req, httplib::Response& res) {
        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "auth_cookie_checker_not_configured"}
            }.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", "", actor_fp, "not_admin");
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
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));
        const std::string registration_request_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "registration_request_b64"));

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (fingerprint.empty() || fingerprint.size() > 160 || routes_v5_has_control_chars(fingerprint)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_fingerprint");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_fingerprint"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(registration_request_b64, 8192)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_registration_request");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_registration_request_b64"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "opaque_backend_not_ready");
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result =
            helper.register_start(status.server_setup_path, login, registration_request_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, helper_err);
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
            routes_v5_audit_password(ctx, req, "opaque.registration_start", "deny", login, actor_fp, "invalid_helper_registration_response");
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_invalid_registration_response"}
            }.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.registration_start", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"registration_response_b64", registration_response_b64},
            {"ready_for_login", false},
            {"warning", "OPAQUE registration start completed, but OPAQUE login/session minting is still intentionally disabled."}
        }.dump());
    });

    // ---- POST /api/admin/auth/opaque/registration/finish ----
    //
    // Admin-only OPAQUE registration finish.
    //
    // This runs OPAQUE registration finish through the helper and stores the
    // resulting serialized server-side password file in opaque_credentials.json.
    // It still does not enable OPAQUE login/session minting.
    srv.Post("/api/admin/auth/opaque/registration/finish", [&](const httplib::Request& req, httplib::Response& res) {
        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "auth_cookie_checker_not_configured"}
            }.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", "", actor_fp, "not_admin");
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
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));
        const std::string registration_upload_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "registration_upload_b64"));
        std::string opaque_suite =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_suite"));

        if (opaque_suite.empty()) {
            opaque_suite = "opaque-ke-4.1.0-pre.0:ristretto255:triple-dh:sha512:argon2";
        }

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (fingerprint.empty() || fingerprint.size() > 160 || routes_v5_has_control_chars(fingerprint)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_fingerprint");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_fingerprint"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(registration_upload_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_registration_upload");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_registration_upload_b64"}}.dump());
            return;
        }

        if (opaque_suite.size() > 128 || routes_v5_has_control_chars(opaque_suite)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_suite");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_suite"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "opaque_backend_not_ready");
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result = helper.register_finish(registration_upload_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, helper_err);
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
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "invalid_helper_password_file");
            reply_json(res, 502, json{
                {"ok", false},
                {"error", "opaque_helper_invalid_password_file"}
            }.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        pqnas::OpaqueCredentialRec rec;
        rec.login = login;
        rec.fingerprint = fingerprint;
        rec.opaque_password_file_b64 = opaque_password_file_b64;
        rec.opaque_suite = opaque_suite;
        rec.enabled = j.value("enabled", true);
        rec.temporary = j.value("temporary", false);

        const auto existing = creds.get(login);
        rec.created_at = existing.has_value() ? existing->created_at : now_iso;
        rec.updated_at = now_iso;

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.registration_finish", "deny", login, actor_fp, "opaque_credentials_save_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.registration_finish", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"opaque_suite", opaque_suite},
            {"enabled", rec.enabled},
            {"temporary", rec.temporary},
            {"ready_for_login", false},
            {"warning", "OPAQUE enrollment was completed and stored, but OPAQUE login/session minting is still intentionally disabled."}
        }.dump());
    });

    // ---- POST /api/admin/auth/opaque/enrollment/upsert ----
    //
    // Admin-only storage scaffold for future OPAQUE enrollment.
    //
    // This endpoint does not run the OPAQUE registration protocol and does not
    // enable OPAQUE login. It only stores a serialized OPAQUE password file
    // produced by a future reviewed OPAQUE registration path.
    srv.Post("/api/admin/auth/opaque/enrollment/upsert", [&](const httplib::Request& req, httplib::Response& res) {
        if (!ctx.require_user_cookie) {
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "server_error"},
                {"message", "auth_cookie_checker_not_configured"}
            }.dump());
            return;
        }

        std::string actor_fp;
        std::string actor_role;
        if (!ctx.require_user_cookie(req, res, &actor_fp, &actor_role)) {
            return;
        }

        if (actor_role != "admin") {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        if (j.contains("password") ||
            j.contains("plaintext_password") ||
            j.contains("password_hash") ||
            j.contains("classic_password_hash") ||
            j.contains("argon2id_hash")) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", "", actor_fp, "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));
        const std::string opaque_password_file_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_password_file_b64"));
        std::string opaque_suite =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_suite"));

        if (opaque_suite.empty()) {
            opaque_suite = "opaque-ke-4.1.0-pre.0:ristretto255:triple-dh:sha512:argon2";
        }

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (fingerprint.empty() || fingerprint.size() > 160 || routes_v5_has_control_chars(fingerprint)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_fingerprint");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_fingerprint"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(opaque_password_file_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_opaque_password_file");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_password_file_b64"}}.dump());
            return;
        }

        if (opaque_suite.size() > 128 || routes_v5_has_control_chars(opaque_suite)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "invalid_suite");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_suite"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(fingerprint);
        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        if (user->email.empty() || pqnas::OpaqueCredentials::normalize_login(user->email) != login) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "login_fingerprint_mismatch");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_fingerprint_mismatch"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!status.credentials_file_exists ||
            !status.credentials_file_readable ||
            !status.credentials_store_valid ||
            !status.server_setup_file_exists ||
            !status.server_setup_file_readable ||
            !status.server_setup_valid ||
            !status.helper_exists ||
            !status.helper_executable ||
            !status.helper_version_ok ||
            !status.helper_self_test_ok) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "opaque_backend_not_ready");
            reply_json(res, 409, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};
        pqnas::OpaqueCredentialRec rec;
        rec.login = login;
        rec.fingerprint = fingerprint;
        rec.opaque_password_file_b64 = opaque_password_file_b64;
        rec.opaque_suite = opaque_suite;
        rec.enabled = j.value("enabled", true);
        rec.temporary = j.value("temporary", false);

        const auto existing = creds.get(login);
        rec.created_at = existing.has_value() ? existing->created_at : now_iso;
        rec.updated_at = now_iso;

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "deny", login, actor_fp, "opaque_credentials_save_failed");
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.enrollment_upsert", "ok", login, actor_fp, fingerprint);

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", fingerprint},
            {"opaque_suite", opaque_suite},
            {"enabled", rec.enabled},
            {"temporary", rec.temporary},
            {"ready_for_login", false},
            {"warning", "OPAQUE enrollment was stored, but OPAQUE login/session minting is still intentionally disabled."}
        }.dump());
    });

    // ---- POST /api/auth/opaque/login/start ----
    //
    // Public OPAQUE login start.
    //
    // This verifies only the OPAQUE transcript start and returns the server
    // credential response plus an opaque in-memory login id. It never accepts
    // plaintext passwords and never mints pqnas_session.
    srv.Post("/api/auth/opaque/login/start", [&](const httplib::Request& req, httplib::Response& res) {
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
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", "", "", "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string login =
            pqnas::OpaqueCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string credential_request_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "credential_request_b64"));

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "invalid_login");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(credential_request_b64, 8192)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "invalid_credential_request");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_credential_request_b64"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("opaque.login_start.") + login,
                ip_for_rate_limit,
                12,
                std::chrono::seconds(60))) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "rate_limited");
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_opaque_login_attempts"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "opaque_backend_not_ready");
            reply_json(res, 503, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueCredentials creds;
        const std::string creds_path = routes_v5_opaque_credentials_path();

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_credentials_load_failed"}}.dump());
            return;
        }

        const auto rec = creds.get(login);
        if (!rec.has_value() ||
            !rec->enabled ||
            rec->opaque_password_file_b64.empty() ||
            !routes_v5_is_safe_b64ish(rec->opaque_password_file_b64, 262144)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, "", "login_missing_or_disabled");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto user_opt = ctx.users->get(rec->fingerprint);
        if (!user_opt.has_value() || user_opt->status != "enabled") {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, "user_disabled_or_missing");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result = helper.login_start(
            status.server_setup_path,
            rec->opaque_password_file_b64,
            login,
            credential_request_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, helper_err);
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const std::string credential_response_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "credential_response_b64"));
        const std::string server_login_state_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(helper_json, "server_login_state_b64"));

        if (!routes_v5_is_safe_b64ish(credential_response_b64, 8192) ||
            !routes_v5_is_safe_b64ish(server_login_state_b64, 16384)) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, "invalid_helper_login_start_response");
            reply_json(res, 502, json{{"ok", false}, {"error", "opaque_helper_invalid_login_start_response"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        routes_v5_opaque_login_pending_prune(now);

        std::string opaque_login_id;
        for (int i = 0; i < 8 && opaque_login_id.empty(); ++i) {
            const std::string candidate = routes_v5_random_hex_id_128();
            if (candidate.empty()) continue;

            RoutesV5OpaqueLoginPending pending;
            pending.login = login;
            pending.fingerprint = rec->fingerprint;
            pending.server_login_state_b64 = server_login_state_b64;
            pending.expires_at = now + 120;

            if (routes_v5_opaque_login_pending_put(candidate, pending)) {
                opaque_login_id = candidate;
            }
        }

        if (opaque_login_id.empty()) {
            routes_v5_audit_password(ctx, req, "opaque.login_start", "deny", login, rec->fingerprint, "opaque_login_state_store_failed");
            reply_json(res, 503, json{{"ok", false}, {"error", "opaque_login_state_store_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "opaque.login_start", "ok", login, rec->fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"opaque_login_id", opaque_login_id},
            {"credential_response_b64", credential_response_b64},
            {"expires_at", now + 120},
            {"ready_for_session", false},
            {"session_minting", false},
            {"warning", "OPAQUE transcript start completed. Session minting happens only after login/finish."}
        }.dump());
    });

    // ---- POST /api/auth/opaque/login/finish ----
    //
    // Public OPAQUE login finish.
    //
    // This proves the OPAQUE transcript using the helper and mints pqnas_session
    // only after successful transcript verification and enabled-user check.
    srv.Post("/api/auth/opaque/login/finish", [&](const httplib::Request& req, httplib::Response& res) {
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
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "forbidden_password_fallback_field");
            reply_json(res, 400, json{{"ok", false}, {"error", "forbidden_password_fallback_field"}}.dump());
            return;
        }

        const std::string opaque_login_id =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "opaque_login_id"));
        const std::string credential_finalization_b64 =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "credential_finalization_b64"));

        if (!routes_v5_is_safe_hex_id_128(opaque_login_id)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "invalid_opaque_login_id");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_opaque_login_id"}}.dump());
            return;
        }

        if (!routes_v5_is_safe_b64ish(credential_finalization_b64, 8192)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "invalid_credential_finalization");
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_credential_finalization_b64"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("opaque.login_finish.") + opaque_login_id,
                ip_for_rate_limit,
                12,
                std::chrono::seconds(60))) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "rate_limited");
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_opaque_login_attempts"}}.dump());
            return;
        }

        const long now = routes_v5_now_epoch_safe(ctx);
        routes_v5_opaque_login_pending_prune(now);

        RoutesV5OpaqueLoginPending pending;
        if (!routes_v5_opaque_login_pending_pop(opaque_login_id, now, pending)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", "", "", "opaque_login_state_missing");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const pqnas::OpaqueBackendStatus status = pqnas::check_opaque_backend_status();
        if (!routes_v5_opaque_backend_ready_for_registration(status)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, "opaque_backend_not_ready");
            reply_json(res, 503, json{{"ok", false}, {"error", "opaque_backend_not_ready"}}.dump());
            return;
        }

        pqnas::OpaqueHelperClient helper(status.helper_path);
        const auto helper_result = helper.login_finish(
            pending.server_login_state_b64,
            credential_finalization_b64);

        json helper_json;
        std::string helper_err;
        if (!routes_v5_helper_result_json(helper_result, helper_json, helper_err)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, helper_err);
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        if (!helper_json.value("authenticated", false)) {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, "not_authenticated");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        if (!ctx.users || !ctx.users_path || ctx.users_path->empty() ||
            !ctx.cookie_key || !ctx.session_cookie_mint) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "opaque_session_mint_not_configured"}}.dump());
            return;
        }

        const auto user = ctx.users->get(pending.fingerprint);
        if (!user.has_value() || user->status != "enabled") {
            routes_v5_audit_password(ctx, req, "opaque.login_finish", "deny", pending.login, pending.fingerprint, "user_disabled_or_missing");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const long sess_iat = now;
        const long sess_exp = sess_iat + (ctx.sess_ttl ? *ctx.sess_ttl : 3600);

        const std::string fp_b64 = routes_v5_b64std_from_string(pending.fingerprint);
        if (fp_b64.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "fingerprint_b64_failed"}}.dump());
            return;
        }

        std::string cookie_val;
        if (!ctx.session_cookie_mint(ctx.cookie_key, fp_b64, sess_iat, sess_exp, cookie_val) ||
            cookie_val.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "cookie_mint_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};
        if (!now_iso.empty()) {
            ctx.users->touch_last_seen(pending.fingerprint, now_iso);
            ctx.users->save(*ctx.users_path);
        }

        const std::string set_cookie =
            std::string("pqnas_session=") + cookie_val +
            "; Path=/" +
            "; HttpOnly" +
            "; SameSite=Strict" +
            "; Secure";

        res.set_header("Set-Cookie", set_cookie);

        routes_v5_audit_password(ctx, req, "opaque.login_finish", "ok", pending.login, pending.fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"authenticated", true},
            {"login", pending.login},
            {"fingerprint", pending.fingerprint},
            {"role", user->role},
            {"expires_at", sess_exp},
            {"ready_for_session", true},
            {"session_minting", true}
        }.dump());
    });

    // ---- POST /api/auth/password/bootstrap-admin ----
    //
    // One-time installer helper. Only works when:
    // - PQNAS_AUTH_MODE=password
    // - PQNAS_PASSWORD_BOOTSTRAP_TOKEN is set
    // - request provides matching X-PQNAS-Bootstrap-Token or bootstrap_token
    // - there is no enabled admin in UsersRegistry yet
    srv.Post("/api/auth/password/bootstrap-admin", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_password_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "password_auth_disabled"}}.dump());
            return;
        }

        const char* token_env = std::getenv("PQNAS_PASSWORD_BOOTSTRAP_TOKEN");
        const std::string expected_token = routes_v5_trim_ascii_copy(token_env ? token_env : "");
        if (expected_token.empty()) {
            reply_json(res, 403, json{{"ok", false}, {"error", "bootstrap_disabled"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                "password.bootstrap",
                ip_for_rate_limit,
                5,
                std::chrono::seconds(300))) {
            routes_v5_audit_password(ctx, req, "password.bootstrap_admin", "deny", "", "", "rate_limited");
            res.set_header("Retry-After", "300");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_bootstrap_attempts"}}.dump());
            return;
        }

        if (!routes_v5_simple_ip_rate_limit_allow(
                "password.bootstrap.global",
                "global",
                20,
                std::chrono::seconds(3600))) {
            routes_v5_audit_password(ctx, req, "password.bootstrap_admin", "deny", "", "", "global_rate_limited");
            res.set_header("Retry-After", "3600");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_bootstrap_attempts"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        const std::string got_token = routes_v5_bootstrap_token_from_req(req, j);
        if (got_token.empty() || got_token != expected_token) {
            reply_json(res, 403, json{{"ok", false}, {"error", "bootstrap_denied"}}.dump());
            return;
        }

        if (!ctx.users || !ctx.users_path || ctx.users_path->empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        pqnas::UserRec existing_admin;
        bool have_existing_admin = false;
        int enabled_admin_count = 0;

        const std::string requested_fingerprint =
            routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));

        {
            const auto snap = ctx.users->snapshot();
            for (const auto& kv : snap) {
                const pqnas::UserRec& candidate = kv.second;
                if (candidate.status == "enabled" && candidate.role == "admin") {
                    ++enabled_admin_count;

                    if (!requested_fingerprint.empty()) {
                        if (candidate.fingerprint == requested_fingerprint) {
                            existing_admin = candidate;
                            have_existing_admin = true;
                        }
                    } else if (enabled_admin_count == 1) {
                        existing_admin = candidate;
                        have_existing_admin = true;
                    }
                }
            }
        }

        if (enabled_admin_count > 1 && requested_fingerprint.empty()) {
            reply_json(res, 409, json{
                {"ok", false},
                {"error", "multiple_admins_require_fingerprint"}
            }.dump());
            return;
        }

        if (!requested_fingerprint.empty() && !have_existing_admin) {
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "admin_fingerprint_not_found"}
            }.dump());
            return;
        }

        const std::string login = pqnas::PasswordCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string password = v5_json_string_or_empty(j, "password");
        const std::string name = routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "name"));

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (password.size() < 12 || password.size() > 1024) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "password_length"}}.dump());
            return;
        }

        pqnas::PasswordCredentials creds;
        const std::string creds_path = routes_v5_password_credentials_path(ctx);
        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_load_failed"}}.dump());
            return;
        }

        if (creds.get(login).has_value()) {
            reply_json(res, 409, json{{"ok", false}, {"error", "login_already_exists"}}.dump());
            return;
        }

        bool bootstrap_created_first_admin = false;
        std::string bootstrap_recovery_words;

        std::string fp_hex;
        if (have_existing_admin) {
            fp_hex = existing_admin.fingerprint;
        } else {
            pqnas::GeneratedDnaIdentity ident;
            std::string gen_error;

            if (!pqnas::generate_dna_identity(ident, gen_error)) {
                // identity_generation_failed_clear_v1
                routes_v5_secure_clear_string(ident.recovery_words);
                routes_v5_audit_password(ctx, req, "password.bootstrap_admin", "deny", login, "", "identity_generation_failed");
                reply_json(res, 500, json{
                    {"ok", false},
                    {"error", "identity_generation_failed"},
                    {"message", gen_error}
                }.dump());
                return;
            }

            fp_hex = ident.fingerprint_hex;
            bootstrap_recovery_words = ident.recovery_words;
            routes_v5_secure_clear_string(ident.recovery_words);
            bootstrap_created_first_admin = true;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        pqnas::UserRec u;
        if (!have_existing_admin) {
            u.fingerprint = fp_hex;
            u.name = name.empty() ? login : name;
            u.role = "admin";
            u.status = "enabled";
            u.added_at = now_iso;
            u.last_seen = "";
            u.notes = "Created by password bootstrap";
            u.group = "";
            u.email = login;
            u.address = "";
            u.avatar_url = "";
            u.storage_state = "unallocated";
            u.quota_bytes = 0;
            u.root_rel = "";
            u.storage_pool_id = "";
            u.storage_set_at = "";
            u.storage_set_by = "";
        }

        std::string hash;
        if (!pqnas::PasswordCredentials::hash_password(password, hash)) {
            routes_v5_secure_clear_string(bootstrap_recovery_words);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "password_hash_failed"}}.dump());
            return;
        }

        pqnas::PasswordCredentialRec rec;
        rec.login = login;
        rec.fingerprint = fp_hex;
        rec.password_hash = hash;
        rec.enabled = true;
        rec.created_at = now_iso;
        rec.updated_at = now_iso;

        if (!have_existing_admin) {
            if (!ctx.users->upsert(u) || !ctx.users->save(*ctx.users_path)) {
                routes_v5_secure_clear_string(bootstrap_recovery_words);
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_save_failed"}}.dump());
                return;
            }
        }

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            if (!have_existing_admin) {
                const bool rolled_back =
                    ctx.users->erase(fp_hex) &&
                    ctx.users->save(*ctx.users_path);
                if (!rolled_back) {
                    routes_v5_audit_password(ctx, req, "password.bootstrap_admin", "deny", login, fp_hex, "user_rollback_failed");
                }
            }

            routes_v5_secure_clear_string(bootstrap_recovery_words);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "password.bootstrap_admin", "ok", login, fp_hex, "");

        json out = {
            {"ok", true},
            {"login", login},
            {"fingerprint", fp_hex},
            {"attached_to_existing_admin", have_existing_admin},
            {"created_first_admin", bootstrap_created_first_admin}
        };

        if (bootstrap_created_first_admin) {
            out["recovery_words_shown_once"] = true;
            out["warning"] = "Recovery words are shown once and are not stored by the server. Remove PQNAS_PASSWORD_BOOTSTRAP_TOKEN after bootstrap.";
        }

        std::string response_body = out.dump();

        if (bootstrap_created_first_admin) {
            std::string recovery_words_json = json(bootstrap_recovery_words).dump();

            if (!routes_v5_append_json_member_to_object(
                    response_body,
                    std::string("\"recovery_words\":") + recovery_words_json)) {
                routes_v5_secure_clear_string(recovery_words_json);
                routes_v5_secure_clear_string(bootstrap_recovery_words);
                routes_v5_secure_clear_string(response_body);
                reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "response_build_failed"}}.dump());
                return;
            }

            routes_v5_secure_clear_string(recovery_words_json);
        }

        routes_v5_secure_clear_string(bootstrap_recovery_words);
        reply_json(res, 200, response_body);
        routes_v5_secure_clear_string(response_body);
    });

    // ---- POST /api/auth/password/login ----
    srv.Post("/api/auth/password/login", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_password_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "password_auth_disabled"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        const std::string login = pqnas::PasswordCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string password = v5_json_string_or_empty(j, "password");

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login) ||
            password.empty() || password.size() > 1024) {
            routes_v5_audit_password(ctx, req, "password.login", "deny", login, "", "invalid_input");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("password.login.") + login,
                ip_for_rate_limit,
                10,
                std::chrono::seconds(60))) {
            routes_v5_audit_password(ctx, req, "password.login", "deny", login, "", "rate_limited");
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_login_attempts"}}.dump());
            return;
        }

        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("password.login.global.") + login,
                "global",
                30,
                std::chrono::seconds(300))) {
            routes_v5_audit_password(ctx, req, "password.login", "deny", login, "", "global_rate_limited");
            res.set_header("Retry-After", "300");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_login_attempts"}}.dump());
            return;
        }

        if (!ctx.users || !ctx.users_path || ctx.users_path->empty() ||
            !ctx.cookie_key || !ctx.session_cookie_mint) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "password_login_not_configured"}}.dump());
            return;
        }

        pqnas::PasswordCredentials creds;
        const std::string creds_path = routes_v5_password_credentials_path(ctx);
        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_load_failed"}}.dump());
            return;
        }

        pqnas::PasswordCredentialRec rec;
        if (!creds.verify_password(login, password, &rec)) {
            routes_v5_audit_password(ctx, req, "password.login", "deny", login, "", "bad_credentials");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
        if (rec.temporary && rec.expires_at_epoch > 0 && now > rec.expires_at_epoch) {
            routes_v5_audit_password(ctx, req, "password.login", "deny", login, rec.fingerprint, "temporary_credential_expired");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const auto user_opt = ctx.users->get(rec.fingerprint);
        if (!user_opt.has_value() || user_opt->status != "enabled") {
            routes_v5_audit_password(ctx, req, "password.login", "deny", login, rec.fingerprint, "user_disabled_or_missing");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        const long sess_exp = now + (ctx.sess_ttl ? *ctx.sess_ttl : 3600);

        const std::string fp_b64 = routes_v5_b64std_from_string(rec.fingerprint);
        if (fp_b64.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "fingerprint_b64_failed"}}.dump());
            return;
        }

        std::string cookie_val;
        if (!ctx.session_cookie_mint(ctx.cookie_key, fp_b64, now, sess_exp, cookie_val) ||
            cookie_val.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "cookie_mint_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};
        if (!now_iso.empty()) {
            ctx.users->touch_last_seen(rec.fingerprint, now_iso);
            ctx.users->save(*ctx.users_path);
        }

        const std::string set_cookie =
            std::string("pqnas_session=") + cookie_val +
            "; Path=/" +
            "; HttpOnly" +
            "; SameSite=Strict" +
            "; Secure";

        res.set_header("Set-Cookie", set_cookie);

        routes_v5_audit_password(ctx, req, "password.login", "ok", login, rec.fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"fingerprint", rec.fingerprint},
            {"role", user_opt->role},
            {"expires_at", sess_exp}
        }.dump());
    });




    // ---- POST /api/auth/password/recover ----
    //
    // Password recovery using CPUNK/DNA 24-word recovery phrase.
    // This does NOT create users and does NOT enable disabled/pending users.
    //
    // Body:
    //   { "login": "user@example.com", "recovery_words": "24 words ...", "new_password": "..." }
    srv.Post("/api/auth/password/recover", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_password_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "password_auth_disabled"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        const std::string login = pqnas::PasswordCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string recovery_words =
            routes_v5_normalize_recovery_words(v5_json_string_or_empty(j, "recovery_words"));
        const std::string new_password = v5_json_string_or_empty(j, "new_password");

        const auto generic_fail = [&]() {
            reply_json(res, 401, json{{"ok", false}, {"error", "recovery_failed"}}.dump());
        };

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login) ||
            recovery_words.empty() || recovery_words.size() > 512) {
            routes_v5_audit_password(ctx, req, "password.recover", "deny", login, "", "invalid_input");
            generic_fail();
            return;
        }

        if (new_password.size() < 12 || new_password.size() > 1024) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "password_length"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("password.recover.") + login,
                ip_for_rate_limit,
                6,
                std::chrono::seconds(300))) {
            routes_v5_audit_password(ctx, req, "password.recover", "deny", login, "", "rate_limited");
            res.set_header("Retry-After", "300");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_recovery_attempts"}}.dump());
            return;
        }

        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("password.recover.global.") + login,
                "global",
                12,
                std::chrono::seconds(900))) {
            routes_v5_audit_password(ctx, req, "password.recover", "deny", login, "", "global_rate_limited");
            res.set_header("Retry-After", "900");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_recovery_attempts"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        pqnas::PasswordCredentials creds;
        const std::string creds_path = routes_v5_password_credentials_path(ctx);

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_load_failed"}}.dump());
            return;
        }

        auto existing = creds.get(login);
        if (!existing.has_value()) {
            routes_v5_audit_password(ctx, req, "password.recover", "deny", login, "", "login_missing");
            generic_fail();
            return;
        }

        pqnas::GeneratedDnaIdentity ident;
        std::string gen_error;
        if (!pqnas::derive_dna_identity_from_recovery_words(recovery_words, ident, gen_error)) {
            routes_v5_audit_password(ctx, req, "password.recover", "deny", login, "", "identity_derivation_failed");
            generic_fail();
            return;
        }

        if (existing->fingerprint != ident.fingerprint_hex) {
            routes_v5_audit_password(ctx, req, "password.recover", "deny", login, "", "fingerprint_mismatch");
            generic_fail();
            return;
        }

        auto user = ctx.users->get(existing->fingerprint);
        if (!user.has_value()) {
            routes_v5_audit_password(ctx, req, "password.recover", "deny", login, existing->fingerprint, "user_missing");
            generic_fail();
            return;
        }

        std::string hash;
        if (!pqnas::PasswordCredentials::hash_password(new_password, hash)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "password_hash_failed"}}.dump());
            return;
        }

        pqnas::PasswordCredentialRec rec = *existing;
        rec.password_hash = hash;
        rec.updated_at = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "password.recover", "ok", login, existing->fingerprint, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"account_status", user->status},
            {"login_allowed", user->status == "enabled"}
        }.dump());
    });

    // ---- POST /api/auth/password/set ----
    //
    // Admin password set/reset endpoint.
    // Requires a valid admin pqnas_session cookie.
    // Body:
    //   { "fingerprint": "...", "login": "user@example.com", "password": "new password" }
    srv.Post("/api/auth/password/set", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_password_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "password_auth_disabled"}}.dump());
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
            routes_v5_audit_password(ctx, req, "password.set", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        const std::string target_fp = routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "fingerprint"));
        const std::string login = pqnas::PasswordCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string password = v5_json_string_or_empty(j, "password");

        if (target_fp.empty() || routes_v5_has_control_chars(target_fp)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_fingerprint"}}.dump());
            return;
        }

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (password.size() < 12 || password.size() > 1024) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "password_length"}}.dump());
            return;
        }

        if (!ctx.users) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        const auto target_user = ctx.users->get(target_fp);
        if (!target_user.has_value()) {
            routes_v5_audit_password(ctx, req, "password.set", "deny", login, target_fp, "target_user_missing");
            reply_json(res, 404, json{{"ok", false}, {"error", "user_not_found"}}.dump());
            return;
        }

        pqnas::PasswordCredentials creds;
        const std::string creds_path = routes_v5_password_credentials_path(ctx);
        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_load_failed"}}.dump());
            return;
        }

        const auto existing_login = creds.get(login);
        if (existing_login.has_value() && existing_login->fingerprint != target_fp) {
            routes_v5_audit_password(ctx, req, "password.set", "deny", login, target_fp, "login_belongs_to_other_fingerprint");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_already_exists"}}.dump());
            return;
        }

        std::string hash;
        if (!pqnas::PasswordCredentials::hash_password(password, hash)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "password_hash_failed"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        pqnas::PasswordCredentialRec rec;
        if (existing_login.has_value()) {
            rec = *existing_login;
        } else {
            rec.login = login;
            rec.fingerprint = target_fp;
            rec.created_at = now_iso;
        }

        rec.login = login;
        rec.fingerprint = target_fp;
        rec.password_hash = hash;
        rec.enabled = true;
        rec.updated_at = now_iso;

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "password.set", "ok", login, target_fp, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", target_fp},
            {"role", target_user->role}
        }.dump());
    });

    // ---- POST /api/auth/password/change ----
    //
    // Self-service password change endpoint.
    // Requires a valid pqnas_session cookie and the current password.
    // Body:
    //   { "login": "user@example.com", "current_password": "...", "new_password": "..." }
    srv.Post("/api/auth/password/change", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_password_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "password_auth_disabled"}}.dump());
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

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        const std::string login = pqnas::PasswordCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string current_password = v5_json_string_or_empty(j, "current_password");
        const std::string new_password = v5_json_string_or_empty(j, "new_password");

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login) ||
            current_password.empty() || current_password.size() > 1024) {
            routes_v5_audit_password(ctx, req, "password.change", "deny", login, actor_fp, "invalid_input");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        if (new_password.size() < 12 || new_password.size() > 1024) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "password_length"}}.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                std::string("password.change.") + login,
                ip_for_rate_limit,
                8,
                std::chrono::seconds(60))) {
            routes_v5_audit_password(ctx, req, "password.change", "deny", login, actor_fp, "rate_limited");
            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{{"ok", false}, {"error", "too_many_password_change_attempts"}}.dump());
            return;
        }

        pqnas::PasswordCredentials creds;
        const std::string creds_path = routes_v5_password_credentials_path(ctx);
        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_load_failed"}}.dump());
            return;
        }

        pqnas::PasswordCredentialRec rec;
        if (!creds.verify_password(login, current_password, &rec) || rec.fingerprint != actor_fp) {
            routes_v5_audit_password(ctx, req, "password.change", "deny", login, actor_fp, "bad_current_password");
            reply_json(res, 401, json{{"ok", false}, {"error", "invalid_login_or_password"}}.dump());
            return;
        }

        std::string hash;
        if (!pqnas::PasswordCredentials::hash_password(new_password, hash)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "password_hash_failed"}}.dump());
            return;
        }

        rec.password_hash = hash;
        rec.enabled = true;
        rec.updated_at = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "password.change", "ok", login, actor_fp, "");

        reply_json(res, 200, json{
            {"ok", true},
            {"login", login},
            {"fingerprint", actor_fp}
        }.dump());
    });


    // ---- POST /api/admin/users/password-create ----
    //
    // Admin-only password-auth user provisioning.
    //
    // This creates a CPUNK/DNA-style identity:
    //   24-word BIP39 recovery phrase
    //   -> deterministic ML-DSA-87 keypair
    //   -> fingerprint = SHA3-512(public key)
    //
    // Recovery words are returned once and are not stored by the server.
    srv.Post("/api/admin/users/password-create", [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_password_auth_enabled()) {
            reply_json(res, 404, json{{"ok", false}, {"error", "password_auth_disabled"}}.dump());
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
            routes_v5_audit_password(ctx, req, "password.user_create", "deny", "", actor_fp, "not_admin");
            reply_json(res, 403, json{{"ok", false}, {"error", "admin_required"}}.dump());
            return;
        }

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        const std::string login = pqnas::PasswordCredentials::normalize_login(v5_json_string_or_empty(j, "login"));
        const std::string password = v5_json_string_or_empty(j, "password");
        const std::string name = routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "name"));
        const bool include_public_key = j.value("include_public_key", false);

        std::uint64_t requested_quota_bytes = 0;
        if (j.contains("quota_bytes") && !j["quota_bytes"].is_null()) {
            if (!j["quota_bytes"].is_number_unsigned()) {
                reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_quota_bytes"}}.dump());
                return;
            }
            requested_quota_bytes = j["quota_bytes"].get<std::uint64_t>();
        }

        std::string role = routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "role")));
        if (role.empty()) role = "user";

        std::string status = routes_v5_lower_ascii_copy(routes_v5_trim_ascii_copy(v5_json_string_or_empty(j, "status")));
        if (status.empty()) status = "disabled";

        if (login.empty() || login.size() > 254 || routes_v5_has_control_chars(login)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_login"}}.dump());
            return;
        }

        if (password.size() < 12 || password.size() > 1024) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "password_length"}}.dump());
            return;
        }

        if (role != "user" && role != "admin") {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_role"}}.dump());
            return;
        }

        if (status != "enabled" && status != "disabled" && status != "pending") {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "invalid_status"}}.dump());
            return;
        }

        if (!ctx.users || !ctx.users_path || ctx.users_path->empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_registry_not_configured"}}.dump());
            return;
        }

        pqnas::PasswordCredentials creds;
        const std::string creds_path = routes_v5_password_credentials_path(ctx);

        if (!creds.load(creds_path)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_load_failed"}}.dump());
            return;
        }

        if (creds.get(login).has_value()) {
            routes_v5_audit_password(ctx, req, "password.user_create", "deny", login, "", "login_already_exists");
            reply_json(res, 409, json{{"ok", false}, {"error", "login_already_exists"}}.dump());
            return;
        }

        pqnas::GeneratedDnaIdentity ident;
        std::string gen_error;
        if (!pqnas::generate_dna_identity(ident, gen_error)) {
            // identity_generation_failed_clear_v1
            routes_v5_secure_clear_string(ident.recovery_words);
            routes_v5_audit_password(ctx, req, "password.user_create", "deny", login, "", "identity_generation_failed");
            reply_json(res, 500, json{
                {"ok", false},
                {"error", "identity_generation_failed"},
                {"message", gen_error}
            }.dump());
            return;
        }

        if (ctx.users->get(ident.fingerprint_hex).has_value()) {
            routes_v5_audit_password(ctx, req, "password.user_create", "deny", login, ident.fingerprint_hex, "fingerprint_collision");
            routes_v5_secure_clear_string(ident.recovery_words);
            reply_json(res, 409, json{{"ok", false}, {"error", "fingerprint_already_exists"}}.dump());
            return;
        }

        const std::string now_iso = ctx.now_iso_utc ? ctx.now_iso_utc() : std::string{};

        pqnas::UserRec u;
        u.fingerprint = ident.fingerprint_hex;
        u.name = name.empty() ? login : name;
        u.role = role;
        u.status = status;
        u.added_at = now_iso;
        u.last_seen = "";
        u.notes = "Created by password-auth provisioning with CPUNK/DNA recovery phrase";
        u.group = "";
        u.email = login;
        u.address = "";
        u.avatar_url = "";
        u.storage_state = "unallocated";
        u.quota_bytes = requested_quota_bytes;
        u.root_rel = "";
        u.storage_pool_id = "";
        u.storage_set_at = "";
        u.storage_set_by = "";

        std::string hash;
        if (!pqnas::PasswordCredentials::hash_password(password, hash)) {
            routes_v5_secure_clear_string(ident.recovery_words);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "password_hash_failed"}}.dump());
            return;
        }

        pqnas::PasswordCredentialRec rec;
        rec.login = login;
        rec.fingerprint = ident.fingerprint_hex;
        rec.password_hash = hash;
        rec.enabled = true;
        rec.created_at = now_iso;
        rec.updated_at = now_iso;

        if (!ctx.users->upsert(u) || !ctx.users->save(*ctx.users_path)) {
            routes_v5_audit_password(ctx, req, "password.user_create", "deny", login, ident.fingerprint_hex, "users_save_failed");
            routes_v5_secure_clear_string(ident.recovery_words);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "users_save_failed"}}.dump());
            return;
        }

        if (!creds.upsert(rec) || !creds.save(creds_path)) {
            routes_v5_audit_password(ctx, req, "password.user_create", "deny", login, ident.fingerprint_hex, "credentials_save_failed");

            const bool rolled_back =
                ctx.users->erase(ident.fingerprint_hex) &&
                ctx.users->save(*ctx.users_path);
            if (!rolled_back) {
                routes_v5_audit_password(ctx, req, "password.user_create", "deny", login, ident.fingerprint_hex, "user_rollback_failed");
            }

            routes_v5_secure_clear_string(ident.recovery_words);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "credentials_save_failed"}}.dump());
            return;
        }

        routes_v5_audit_password(ctx, req, "password.user_create", "ok", login, ident.fingerprint_hex, "");

        json out = {
            {"ok", true},
            {"login", login},
            {"fingerprint", ident.fingerprint_hex},
            {"role", role},
            {"status", status},
            {"quota_bytes", requested_quota_bytes},
            {"recovery_words_shown_once", true},
            {"warning", "Recovery words are shown once and are not stored by the server."}
        };

        if (include_public_key) {
            out["public_key_b64"] = ident.public_key_b64;
        }

        std::string response_body = out.dump();
        std::string recovery_words_json = json(ident.recovery_words).dump();

        if (!routes_v5_append_json_member_to_object(
                response_body,
                std::string("\"recovery_words\":") + recovery_words_json)) {
            routes_v5_secure_clear_string(recovery_words_json);
            routes_v5_secure_clear_string(ident.recovery_words);
            routes_v5_secure_clear_string(response_body);
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "response_build_failed"}}.dump());
            return;
        }

        routes_v5_secure_clear_string(recovery_words_json);
        routes_v5_secure_clear_string(ident.recovery_words);
        reply_json(res, 200, response_body);
        routes_v5_secure_clear_string(response_body);
    });

    // ---- POST/GET /api/v5/session ----
	// Route group: /api/v5/session
	// Issues a signed request token (st) and correlation key (k). Inserts PendingEntry.

    auto session_handler = [&](const httplib::Request& req, httplib::Response& res) {
        if (!routes_v5_qr_auth_enabled()) {
            reply_json(res, 404, json{
                {"ok", false},
                {"error", "qr_auth_disabled"},
                {"mode", routes_v5_auth_mode()}
            }.dump());
            return;
        }

        const std::string ip_for_rate_limit = routes_v5_request_ip(ctx, req);
        if (!routes_v5_simple_ip_rate_limit_allow(
                "v5.session",
                ip_for_rate_limit,
                20,
                std::chrono::seconds(60))) {
            if (ctx.audit_emit) {
                ctx.audit_emit("rate_limited", "deny", [&](std::map<std::string,std::string>& f) {
                    f["path"] = "/api/v5/session";
                    f["ip"] = ip_for_rate_limit;
                    f["limit"] = "20_per_60s";
                });
            }

            res.set_header("Retry-After", "60");
            reply_json(res, 429, json{
                {"ok", false},
                {"error", "too_many_session_requests"}
            }.dump());
            return;
        }

        const long now = ctx.now_epoch ? ctx.now_epoch() : 0;

        // prune old entries first
        if (ctx.approvals_prune) ctx.approvals_prune(now);
        if (ctx.pending_prune)   ctx.pending_prune(now);

        // mint session request token (st)
        const std::string sid   = ctx.random_b64url ? ctx.random_b64url(18) : std::string{};
        const std::string chal  = ctx.random_b64url ? ctx.random_b64url(32) : std::string{};
        const std::string nonce = ctx.random_b64url ? ctx.random_b64url(18) : std::string{};
        if (sid.empty() || chal.empty() || nonce.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "rng_failed"}}.dump());
            return;
        }

        const long iat = now;
        const long exp = now + (ctx.req_ttl ? *ctx.req_ttl : 120);

        if (!ctx.build_req_payload_canonical || !ctx.sign_req_token) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "missing_callbacks"}}.dump());
            return;
        }

        const std::string payload = ctx.build_req_payload_canonical(sid, chal, nonce, iat, exp);
        const std::string st      = ctx.sign_req_token(payload);
        if (st.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "sign_failed"}}.dump());
            return;
        }

        // derive k (stateless-ready correlation key)
        std::string k;
        if (ctx.st_hash_b64_from_st) k = ctx.st_hash_b64_from_st(st);
        if (k.empty()) {
            // We require k for 2A flow
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "k_derive_failed"}}.dump());
            return;
        }

        // mark pending so status can return "pending" immediately
        if (!ctx.pending_put) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "pending_put_not_configured"}}.dump());
            return;
        }

        const std::string preauth = ctx.random_b64url ? ctx.random_b64url(32) : std::string{};
        if (preauth.empty()) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "preauth_rng_failed"}}.dump());
            return;
        }

        const std::string preauth_hash = sha256_hex(preauth);

        RoutesV5Context::PendingEntry pe;
        pe.expires_at = exp;
        pe.reason = "awaiting_scan";
        pe.browser_bind_hash = preauth_hash;
        ctx.pending_put(k, pe);

        if (!(ctx.pending_get)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "pending_get_not_configured"}}.dump());
            return;
        }

        RoutesV5Context::PendingEntry verify_pe;
        if (!ctx.pending_get(k, verify_pe)) {
            reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "pending_put_failed"}}.dump());
            return;
        }

        const long max_age = (exp > now) ? (exp - now) : 0;
        res.set_header("Set-Cookie", make_preauth_cookie(preauth, max_age));

        // audit (optional)
        if (ctx.audit_emit) {
            ctx.audit_emit("v5.session_issued", "ok", [&](std::map<std::string,std::string>& f) {
                f["sid"] = sid;
                f["k"] = k;
                f["now"] = std::to_string(now);
                f["iat"] = std::to_string(iat);
                f["exp"] = std::to_string(exp);
                if (ctx.client_ip) f["ip"] = ctx.client_ip(req);
            });
        }

        reply_json(res, 200, json{
            {"ok", true},
            {"sid", sid},
            {"st", st},
            {"k", k},
            {"iat", iat},
            {"exp", exp},
            {"qr_svg", std::string("/api/v5/qr.svg?st=") + (ctx.url_encode ? ctx.url_encode(st) : st)}
        }.dump());
    };

    srv.Post("/api/v5/session", session_handler);
    srv.Get ("/api/v5/session", session_handler); // harmless / useful for debugging

    // ---- GET /api/v5/qr.svg?st=... ----
    srv.Get("/api/v5/qr.svg", [&](const httplib::Request& req, httplib::Response& res) {
        auto it = req.params.find("st");
        if (it == req.params.end() || it->second.empty()) {
            reply_json(res, 400, json({{"ok", false}, {"error", "bad_request"}, {"message", "missing st"}}).dump());
            return;
        }

        const std::string st = it->second;
        const std::string qr_uri =
            "dna://auth?v=5&st=" + (ctx.url_encode ? ctx.url_encode(st) : st) +
            "&origin=" + (ctx.url_encode ? ctx.url_encode(*ctx.origin) : *ctx.origin) +
            "&app=" + (ctx.url_encode ? ctx.url_encode(*ctx.app) : *ctx.app);

        try {
            if (!ctx.qr_svg_from_text) throw std::runtime_error("missing qr_svg_from_text");
            const std::string svg = ctx.qr_svg_from_text(qr_uri, 6, 4);
            res.status = 200;
            res.set_header("Content-Type", "image/svg+xml; charset=utf-8");
            res.set_header("Cache-Control", "no-store");
            res.body = svg;
        } catch (const std::exception& e) {
            reply_json(res, 500, json({{"ok", false}, {"error", "server_error"}, {"message", e.what()}}).dump());
        }
    });

    // ---- GET /api/v5/status?k=...|sid=... (and optionally st=...) ----
    srv.Get("/api/v5/status", [&](const httplib::Request& req, httplib::Response& res) {
        const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
        if (ctx.approvals_prune) ctx.approvals_prune(now);
        if (ctx.pending_prune)   ctx.pending_prune(now);

        std::string key, err;
        if (!resolve_approval_key_from_req(ctx, req, nullptr, key, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

        RoutesV5Context::ApprovalEntry ae;
        if (ctx.approvals_get && ctx.approvals_get(key, ae)) {
            reply_json(res, 200, json{{"ok", true}, {"approved", true}, {"k", key}, {"expires_at", ae.expires_at}}.dump());
            return;
        }

        RoutesV5Context::PendingEntry pe;
        if (ctx.pending_get && ctx.pending_get(key, pe)) {
            reply_pending_or_promoted_status(ctx, res, key, pe, now);
            return;
        }


        reply_json(res, 200, json{{"ok", true}, {"approved", false}}.dump());
    });

    // ---- 2A: POST /api/v5/status {st} ----
    srv.Post("/api/v5/status", [&](const httplib::Request& req, httplib::Response& res) {
        const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
        if (ctx.approvals_prune) ctx.approvals_prune(now);
        if (ctx.pending_prune)   ctx.pending_prune(now);

        json j;
        std::string err;
        if (!parse_json_body(req, j, err)) {
            reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
            return;
        }

		std::string k;
		if (!get_key_from_json(ctx, j, k, err)) {
    		reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", err}}.dump());
    		return;
		}


        RoutesV5Context::ApprovalEntry ae;
        if (ctx.approvals_get && ctx.approvals_get(k, ae)) {
            reply_json(res, 200, json{
                {"ok", true},
                {"approved", true},
                {"state", "approved"},
                {"k", k},
                {"expires_at", ae.expires_at}
            }.dump());
            return;
        }

        RoutesV5Context::PendingEntry pe;
        if (ctx.pending_get && ctx.pending_get(k, pe)) {
            reply_pending_or_promoted_status(ctx, res, k, pe, now);
            return;
        }

        reply_json(res, 200, json{
            {"ok", true},
            {"approved", false},
            {"state", "missing"},
            {"k", k}
        }.dump());
    });

// ---- 2A: POST /api/v5/consume {st|k|sid} ----
// Route group: /api/v5/session
// Issues a signed request token (st) and correlation key (k). Inserts PendingEntry.
srv.Post("/api/v5/consume", [&](const httplib::Request& req, httplib::Response& res) {
    const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
    if (ctx.approvals_prune) ctx.approvals_prune(now);
    if (ctx.pending_prune)   ctx.pending_prune(now);

    json j;
    std::string jerr;
    if (!parse_json_body(req, j, jerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", jerr}}.dump());
        return;
    }

    std::string key, kerr;
    if (!resolve_approval_key_from_req(ctx, req, &j, key, kerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", kerr}}.dump());
        return;
    }

    RoutesV5Context::PendingEntry pe;
    if (!(ctx.pending_get && ctx.pending_get(key, pe))) {
        reply_json(res, 409, json{
            {"ok", false},
            {"error", "session_missing"},
            {"k", key}
        }.dump());
        return;
    }

    const std::string preauth = get_cookie_value(req, "pqnas_preauth");
    if (preauth.empty()) {
        reply_json(res, 428, json{
            {"ok", false},
            {"error", "preauth_required"},
            {"message", "missing browser binding cookie"},
            {"k", key}
        }.dump());
        return;
    }

    if (pe.browser_bind_hash.empty() || sha256_hex(preauth) != pe.browser_bind_hash) {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "browser_binding_failed"},
            {"message", "browser binding check failed"},
            {"k", key}
        }.dump());
        return;
    }

    RoutesV5Context::ApprovalEntry ae;
    if (!(ctx.approvals_get && ctx.approvals_get(key, ae))) {
        reply_json(res, 409, json{{"ok", false}, {"error", "not_approved"}, {"k", key}}.dump());
        return;
    }

    // If cookie is missing, fail loudly
    if (ae.cookie_val.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "cookie_empty"}, {"k", key}}.dump());
        return;
    }

    // Build full Set-Cookie header (name=value; attributes)
    //
    // NOTE: ae.cookie_val is ONLY the cookie VALUE (our signed token),
    // not a full "Set-Cookie:" header. The browser will ignore it unless
    // we provide "pqnas_session=<value>; Path=/; ...".
    //
    // SameSite=Strict prevents cross-site POSTs from carrying pqnas_session.
    const std::string set_cookie =
        std::string("pqnas_session=") + ae.cookie_val +
        "; Path=/" +
        "; HttpOnly" +
        "; SameSite=Strict" +
        "; Secure";

    // IMPORTANT:
    // - issue the real authenticated session cookie
    // - immediately clear the one-time browser binding cookie
    //
    // We need TWO Set-Cookie headers here.
    const std::string clear_cookie = clear_preauth_cookie();

    res.headers.emplace("Set-Cookie", set_cookie);
    res.headers.emplace("Set-Cookie", clear_cookie);

    if (ctx.audit_emit) {
        ctx.audit_emit("v5.consume_cookie", "ok", [&](std::map<std::string,std::string>& f) {
            f["k"] = key;
            f["cookie_len"] = std::to_string(ae.cookie_val.size());
            f["set_cookie_len"] = std::to_string(set_cookie.size());
            f["clear_cookie_len"] = std::to_string(clear_cookie.size());
        });
    }

    // Now that we have everything, consume the one-time approval + pending entries.
    if (ctx.approvals_pop) ctx.approvals_pop(key);
    if (ctx.pending_pop)   ctx.pending_pop(key);

    reply_json(res, 200, json{
        {"ok", true},
        {"state", "consumed"},
        {"k", key}
    }.dump());
});

// ---- POST /api/v5/consume_app {st|k|sid, device_name?, platform?, app_version?} ----
// Mobile/app equivalent of /consume: returns bearer tokens instead of Set-Cookie.
srv.Post("/api/v5/consume_app", [&](const httplib::Request& req, httplib::Response& res) {
    const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
    if (ctx.approvals_prune) ctx.approvals_prune(now);
    if (ctx.pending_prune)   ctx.pending_prune(now);

    json j;
    std::string jerr;
    if (!parse_json_body(req, j, jerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", jerr}}.dump());
        return;
    }

    std::string key, kerr;
    if (!resolve_approval_key_from_req(ctx, req, &j, key, kerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", kerr}}.dump());
        return;
    }

    RoutesV5Context::ApprovalEntry ae;
    if (!(ctx.approvals_get && ctx.approvals_get(key, ae))) {
        reply_json(res, 409, json{{"ok", false}, {"error", "not_approved"}, {"k", key}}.dump());
        return;
    }

    if (ae.fingerprint.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "approval_missing_fingerprint"}, {"k", key}}.dump());
        return;
    }

    if (!ctx.consume_app_mint) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "consume_app_not_configured"}, {"k", key}}.dump());
        return;
    }

	const std::string device_name = j.value("device_name", std::string{});
	std::string platform = j.value("platform", std::string{});
	const std::string app_version = j.value("app_version", std::string{});
	const std::string device_model = j.value("device_model", std::string{});
	const std::string device_manufacturer = j.value("device_manufacturer", std::string{});
	const std::string os_version = j.value("os_version", std::string{});
	if (platform.empty()) platform = "android";

    RoutesV5Context::ConsumeAppResult out;
    std::string merr;
    const std::string client_ip = ctx.client_ip ? ctx.client_ip(req) : req.remote_addr;

    if (!ctx.consume_app_mint(ae.fingerprint,
                          device_name,
                          platform,
                          app_version,
                          device_model,
                          device_manufacturer,
                          os_version,
                          client_ip,
                          out,
                          merr)) {
        if (ctx.audit_emit) {
            ctx.audit_emit("v5.consume_app_fail", "fail", [&](std::map<std::string,std::string>& f) {
                f["k"] = key;
                f["fingerprint"] = ae.fingerprint;
                f["reason"] = merr.empty() ? "mint_failed" : merr;
                if (!platform.empty()) f["platform"] = platform;
                if (!device_name.empty()) f["device_name"] = device_name;
                if (!app_version.empty()) f["app_version"] = app_version;
				if (!device_model.empty()) f["device_model"] = device_model;
				if (!device_manufacturer.empty()) f["device_manufacturer"] = device_manufacturer;
				if (!os_version.empty()) f["os_version"] = os_version;
                if (!client_ip.empty()) f["ip"] = client_ip;
                const std::string ua = audit_safe_header_value(req_header_or_empty(req, "User-Agent"));
                if (!ua.empty()) f["ua"] = ua;
            });
        }

        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", merr.empty() ? "app consume denied" : merr},
            {"k", key}
        }.dump());
        return;
    }

    if (ctx.approvals_pop) ctx.approvals_pop(key);

    if (ctx.audit_emit) {
        ctx.audit_emit("v5.consume_app_ok", "ok", [&](std::map<std::string,std::string>& f) {
            f["k"] = key;
            f["fingerprint"] = out.fingerprint_hex.empty() ? ae.fingerprint : out.fingerprint_hex;
            f["device_id"] = out.device_id;
            f["role"] = out.role;
            f["platform"] = platform;
            if (!device_name.empty()) f["device_name"] = device_name;
            if (!app_version.empty()) f["app_version"] = app_version;
			if (!device_model.empty()) f["device_model"] = device_model;
			if (!device_manufacturer.empty()) f["device_manufacturer"] = device_manufacturer;
			if (!os_version.empty()) f["os_version"] = os_version;
            if (!client_ip.empty()) f["ip"] = client_ip;
            const std::string ua = audit_safe_header_value(req_header_or_empty(req, "User-Agent"));
            if (!ua.empty()) f["ua"] = ua;
        });
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"token_type", "Bearer"},
        {"access_token", out.access_token},
        {"expires_in", (out.access_exp > now ? out.access_exp - now : 0)},
        {"refresh_token", out.refresh_token},
        {"refresh_expires_in", (out.refresh_exp > now ? out.refresh_exp - now : 0)},
        {"device_id", out.device_id},
        {"fingerprint_hex", out.fingerprint_hex.empty() ? ae.fingerprint : out.fingerprint_hex},
        {"role", out.role}
    }.dump());
});

// ---- POST /api/v5/token/refresh {refresh_token, device_id} ----
// Mobile/app access-token refresh.
srv.Post("/api/v5/token/refresh", [&](const httplib::Request& req, httplib::Response& res) {
    json j;
    std::string jerr;
    if (!parse_json_body(req, j, jerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", jerr}}.dump());
        return;
    }

    const std::string refresh_token = j.value("refresh_token", std::string{});
    const std::string device_id = j.value("device_id", std::string{});
    if (refresh_token.empty() || device_id.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing refresh_token or device_id"}}.dump());
        return;
    }

    if (!ctx.refresh_app_token) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "refresh_app_token_not_configured"}}.dump());
        return;
    }

    RoutesV5Context::RefreshAppResult out;
    std::string rerr;
    const std::string client_ip = ctx.client_ip ? ctx.client_ip(req) : req.remote_addr;

    if (!ctx.refresh_app_token(refresh_token, device_id, client_ip, out, rerr)) {
        if (ctx.audit_emit) {
            ctx.audit_emit("v5.token_refresh_fail", "fail", [&](std::map<std::string,std::string>& f) {
                f["device_id"] = device_id;
                f["reason"] = rerr.empty() ? "refresh_failed" : rerr;
                if (!client_ip.empty()) f["ip"] = client_ip;
                const std::string ua = audit_safe_header_value(req_header_or_empty(req, "User-Agent"));
                if (!ua.empty()) f["ua"] = ua;
            });
        }

        reply_json(res, 401, json{
            {"ok", false},
            {"error", "unauthorized"},
            {"message", rerr.empty() ? "refresh denied" : rerr},
            {"device_id", device_id}
        }.dump());
        return;
    }

    if (ctx.audit_emit) {
        ctx.audit_emit("v5.token_refresh_ok", "ok", [&](std::map<std::string,std::string>& f) {
            f["device_id"] = out.device_id.empty() ? device_id : out.device_id;
            if (!out.fingerprint_hex.empty()) f["fingerprint"] = out.fingerprint_hex;
            if (!out.role.empty()) f["role"] = out.role;
            if (!client_ip.empty()) f["ip"] = client_ip;
            const std::string ua = audit_safe_header_value(req_header_or_empty(req, "User-Agent"));
            if (!ua.empty()) f["ua"] = ua;
        });
    }

    const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
    reply_json(res, 200, json{
        {"ok", true},
        {"token_type", "Bearer"},
        {"access_token", out.access_token},
        {"expires_in", (out.access_exp > now ? out.access_exp - now : 0)},
        {"fingerprint_hex", out.fingerprint_hex},
        {"role", out.role},
        {"device_id", out.device_id.empty() ? device_id : out.device_id}
    }.dump());
});
// ---- POST /api/v5/token/revoke {refresh_token, device_id} ----
// Mobile/app logout endpoint.
// Requires possession of the refresh token, then revokes the whole app-device session.
srv.Post("/api/v5/token/revoke", [&](const httplib::Request& req, httplib::Response& res) {
    json j;
    std::string jerr;

    if (!parse_json_body(req, j, jerr)) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", jerr}
        }.dump());
        return;
    }

    const std::string refresh_token = j.value("refresh_token", std::string{});
    const std::string device_id = j.value("device_id", std::string{});

    if (refresh_token.empty() || device_id.empty()) {
        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", "missing refresh_token or device_id"}
        }.dump());
        return;
    }

    if (!ctx.revoke_app_token) {
        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", "revoke_app_token_not_configured"}
        }.dump());
        return;
    }

    std::string rerr;
    const std::string client_ip = ctx.client_ip ? ctx.client_ip(req) : req.remote_addr;

    if (!ctx.revoke_app_token(refresh_token, device_id, client_ip, rerr)) {
        if (ctx.audit_emit) {
            ctx.audit_emit("v5.token_revoke_fail", "fail", [&](std::map<std::string, std::string>& f) {
                f["device_id"] = device_id;
                f["reason"] = rerr.empty() ? "revoke_failed" : rerr;
                if (!client_ip.empty()) f["ip"] = client_ip;

                const std::string ua = audit_safe_header_value(req_header_or_empty(req, "User-Agent"));
                if (!ua.empty()) f["ua"] = ua;
            });
        }

        reply_json(res, 401, json{
            {"ok", false},
            {"error", "unauthorized"},
            {"message", rerr.empty() ? "revoke denied" : rerr},
            {"device_id", device_id}
        }.dump());
        return;
    }

    if (ctx.audit_emit) {
        ctx.audit_emit("v5.token_revoke_ok", "ok", [&](std::map<std::string, std::string>& f) {
            f["device_id"] = device_id;
            if (!client_ip.empty()) f["ip"] = client_ip;

            const std::string ua = audit_safe_header_value(req_header_or_empty(req, "User-Agent"));
            if (!ua.empty()) f["ua"] = ua;
        });
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"device_id", device_id},
        {"state", "revoked"}
    }.dump());
});
// ---- POST /api/v5/app_pair/start ----
srv.Post("/api/v5/app_pair/start", [&](const httplib::Request& req, httplib::Response& res) {
    if (!ctx.require_user_cookie) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "require_user_cookie_not_configured"}}.dump());
        return;
    }

    std::string fp_hex, role;
    if (!ctx.require_user_cookie(req, res, &fp_hex, &role)) return;

    const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
    if (ctx.app_pair_prune) ctx.app_pair_prune(now);

    if (!ctx.app_pair_start) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "app_pair_start_not_configured"}}.dump());
        return;
    }

    RoutesV5Context::AppPairStartResult out;
    std::string err;
    if (!ctx.app_pair_start(fp_hex, role, out, err)) {
        audit_v5_req(ctx, "v5.app_pair_start_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["fingerprint"] = fp_hex;
            f["role"] = role;
            f["reason"] = err.empty() ? "pair_start_failed" : err;
        });

        reply_json(res, 500, json{
            {"ok", false},
            {"error", "server_error"},
            {"message", err.empty() ? "pair start failed" : err}
        }.dump());
        return;
    }

    audit_v5_req(ctx, "v5.app_pair_start_ok", "ok", req, [&](std::map<std::string,std::string>& f) {
        f["fingerprint"] = fp_hex;
        f["role"] = role;
        f["pair_id"] = out.pair_id;
        f["expires_at"] = std::to_string(out.expires_at);
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"pair_id", out.pair_id},
        {"expires_at", out.expires_at},
        {"qr_uri", out.qr_uri},
        {"tls_pin_sha256", (ctx.tls_spki_sha256_pin ? *ctx.tls_spki_sha256_pin : std::string{})},
        {"qr_svg", std::string("/api/v5/app_pair/qr.svg?id=") + (ctx.url_encode ? ctx.url_encode(out.pair_id) : out.pair_id)}
    }.dump());
});

srv.Post("/api/v5/app_pair/cancel", [&](const httplib::Request& req, httplib::Response& res) {
    if (!ctx.require_user_cookie) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "require_user_cookie_not_configured"}}.dump());
        return;
    }

    std::string fp_hex, role;
    if (!ctx.require_user_cookie(req, res, &fp_hex, &role)) return;

    json j;
    std::string jerr;
    if (!parse_json_body(req, j, jerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", jerr}}.dump());
        return;
    }

    const std::string pair_id = j.value("pair_id", std::string{});
    if (pair_id.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing pair_id"}}.dump());
        return;
    }

    if (!ctx.app_pair_get || !ctx.app_pair_cancel) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "app_pair cancel dependencies missing"}}.dump());
        return;
    }

    RoutesV5Context::AppPairStatusResult st;
    std::string err;
    if (!ctx.app_pair_get(pair_id, st, err)) {
        audit_v5_req(ctx, "v5.app_pair_cancel_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["fingerprint"] = fp_hex;
            f["role"] = role;
            f["pair_id"] = pair_id;
            f["reason"] = "pairing_not_found";
        });

        reply_json(res, 404, json{{"ok", false}, {"error", "not_found"}, {"message", "pairing not found"}, {"pair_id", pair_id}}.dump());
        return;
    }

    if (st.fingerprint_hex != fp_hex) {
        audit_v5_req(ctx, "v5.app_pair_cancel_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["fingerprint"] = fp_hex;
            f["role"] = role;
            f["pair_id"] = pair_id;
            f["reason"] = "owner_mismatch";
        });

        reply_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "pairing does not belong to current user"}}.dump());
        return;
    }

    if (!ctx.app_pair_cancel(pair_id, err)) {
        audit_v5_req(ctx, "v5.app_pair_cancel_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["fingerprint"] = fp_hex;
            f["role"] = role;
            f["pair_id"] = pair_id;
            f["reason"] = err.empty() ? "cancel_failed" : err;
        });

        reply_json(res, 409, json{{"ok", false}, {"error", "not_allowed"}, {"message", err.empty() ? "cancel failed" : err}, {"pair_id", pair_id}}.dump());
        return;
    }

    audit_v5_req(ctx, "v5.app_pair_cancel_ok", "ok", req, [&](std::map<std::string,std::string>& f) {
        f["fingerprint"] = fp_hex;
        f["role"] = role;
        f["pair_id"] = pair_id;
    });

    reply_json(res, 200, json{{"ok", true}, {"pair_id", pair_id}, {"state", "cancelled"}}.dump());
});

srv.Get("/api/v5/app_devices", [&](const httplib::Request& req, httplib::Response& res) {
    if (!ctx.require_user_cookie) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "require_user_cookie_not_configured"}}.dump());
        return;
    }

    std::string fp_hex, role;
    if (!ctx.require_user_cookie(req, res, &fp_hex, &role)) return;

    if (!ctx.app_devices_list_for_fingerprint) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "app_devices_list_for_fingerprint_not_configured"}}.dump());
        return;
    }

    const auto devices = ctx.app_devices_list_for_fingerprint(fp_hex);

    json arr = json::array();

	for (const auto& d : devices) {
    	if (d.revoked) continue;

	    long refresh_expires_at = 0;
    	const bool has_refresh_expiry =
        	ctx.app_device_refresh_expiry &&
	        ctx.app_device_refresh_expiry(d.device_id, refresh_expires_at);

		arr.push_back(json{
    		{"device_id", d.device_id},
	    	{"role", d.role},
	    	{"platform", d.platform},
		    {"device_name", d.device_name},
    		{"app_version", d.app_version},
		    {"device_model", d.device_model},
    		{"device_manufacturer", d.device_manufacturer},
	    	{"os_version", d.os_version},
	    	{"created_at", d.created_at},
		    {"last_seen_at", d.last_seen_at},
    		{"last_ip", d.last_ip},
	    	{"revoked", d.revoked},
		    {"refresh_expires_at", has_refresh_expiry ? refresh_expires_at : 0}
		});
	}

    reply_json(res, 200, json{
        {"ok", true},
        {"devices", arr}
    }.dump());
});

srv.Post("/api/v5/app_devices/revoke", [&](const httplib::Request& req, httplib::Response& res) {
    if (!ctx.require_user_cookie) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "require_user_cookie_not_configured"}}.dump());
        return;
    }

    std::string fp_hex, role;
    if (!ctx.require_user_cookie(req, res, &fp_hex, &role)) return;

    json j;
    std::string jerr;
    if (!parse_json_body(req, j, jerr)) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", jerr}}.dump());
        return;
    }

    const std::string device_id = j.value("device_id", std::string{});
    if (device_id.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing device_id"}}.dump());
        return;
    }

    if (!ctx.app_device_get || !ctx.app_device_revoke) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "app device revoke dependencies missing"}}.dump());
        return;
    }

    pqnas::TrustedAppDevice d;
    if (!ctx.app_device_get(device_id, d)) {
        audit_v5_req(ctx, "v5.app_device_revoke_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["fingerprint"] = fp_hex;
            f["role"] = role;
            f["device_id"] = device_id;
            f["reason"] = "device_not_found";
        });

        reply_json(res, 404, json{{"ok", false}, {"error", "not_found"}, {"message", "device not found"}, {"device_id", device_id}}.dump());
        return;
    }

    if (d.fingerprint_hex != fp_hex) {
        audit_v5_req(ctx, "v5.app_device_revoke_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["fingerprint"] = fp_hex;
            f["role"] = role;
            f["device_id"] = device_id;
            f["reason"] = "owner_mismatch";
        });

        reply_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "device does not belong to current user"}}.dump());
        return;
    }

    std::string err;
    if (!ctx.app_device_revoke(device_id, err)) {
        audit_v5_req(ctx, "v5.app_device_revoke_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["fingerprint"] = fp_hex;
            f["role"] = role;
            f["device_id"] = device_id;
            f["reason"] = err.empty() ? "revoke_failed" : err;
            if (!d.platform.empty()) f["platform"] = d.platform;
            if (!d.app_version.empty()) f["app_version"] = d.app_version;
        });

        reply_json(res, 409, json{{"ok", false}, {"error", "not_allowed"}, {"message", err.empty() ? "revoke failed" : err}, {"device_id", device_id}}.dump());
        return;
    }

    audit_v5_req(ctx, "v5.app_device_revoke_ok", "ok", req, [&](std::map<std::string,std::string>& f) {
        f["fingerprint"] = fp_hex;
        f["role"] = role;
        f["device_id"] = device_id;
        if (!d.platform.empty()) f["platform"] = d.platform;
        if (!d.app_version.empty()) f["app_version"] = d.app_version;
    });

    reply_json(res, 200, json{
        {"ok", true},
        {"device_id", device_id},
        {"state", "revoked"}
    }.dump());
});

// ---- GET /api/v5/app_pair/qr.svg?id=... ----
// Authenticated browser-only QR renderer.
// The URL carries only pair_id. The secret pair_token is looked up server-side
// so it does not leak into access logs, proxy logs, browser history, or Referer.
srv.Get("/api/v5/app_pair/qr.svg", [&](const httplib::Request& req, httplib::Response& res) {
    if (!ctx.require_user_cookie) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "missing require_user_cookie"}}.dump());
        return;
    }

    std::string fp_hex;
    std::string role;
    if (!ctx.require_user_cookie(req, res, &fp_hex, &role)) {
        return; // helper already wrote response
    }

    auto it = req.params.find("id");
    if (it == req.params.end() || it->second.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing id"}}.dump());
        return;
    }

    const std::string pair_id = it->second;

    RoutesV5Context::AppPairStatusResult st;
    std::string err;
    if (!(ctx.app_pair_get && ctx.app_pair_get(pair_id, st, err))) {
        reply_json(res, 404, json{{"ok", false}, {"error", "not_found"}, {"message", err.empty() ? "pair_id_not_found" : err}}.dump());
        return;
    }

    if (st.fingerprint_hex != fp_hex) {
        reply_json(res, 403, json{{"ok", false}, {"error", "forbidden"}, {"message", "pairing belongs to another user"}}.dump());
        return;
    }

    if (st.pair_token.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "pair_token_missing"}}.dump());
        return;
    }

    if (!ctx.origin || !ctx.app || !ctx.tls_spki_sha256_pin || ctx.tls_spki_sha256_pin->empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "tls pin not configured"}}.dump());
        return;
    }

    const std::string qr_uri =
        ctx.app_pair_build_qr_uri
            ? ctx.app_pair_build_qr_uri(*ctx.origin, st.pair_token, *ctx.app, *ctx.tls_spki_sha256_pin)
            : std::string{};

    if (qr_uri.empty()) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "qr_uri_failed"}}.dump());
        return;
    }

    try {
        if (!ctx.qr_svg_from_text) throw std::runtime_error("missing qr_svg_from_text");
        const std::string svg = ctx.qr_svg_from_text(qr_uri, 6, 4);

        res.status = 200;
        res.set_header("Content-Type", "image/svg+xml; charset=utf-8");
        res.set_header("Cache-Control", "no-store");
        res.body = svg;
    } catch (const std::exception& e) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", e.what()}}.dump());
    }
});
// ---- GET /api/v5/app_pair/status?pair_id=... ----
srv.Get("/api/v5/app_pair/status", [&](const httplib::Request& req, httplib::Response& res) {
    if (!ctx.require_user_cookie) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "missing require_user_cookie"}}.dump());
        return;
    }

    std::string fp_hex;
    std::string role;
    if (!ctx.require_user_cookie(req, res, &fp_hex, &role)) {
        return; // helper already wrote response
    }

    auto it = req.params.find("pair_id");
    if (it == req.params.end() || it->second.empty()) {
        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", "missing pair_id"}}.dump());
        return;
    }

    const std::string pair_id = it->second;
    const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
    if (ctx.app_pair_prune) ctx.app_pair_prune(now);

    if (!ctx.app_pair_get) {
        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "app_pair_get_not_configured"}}.dump());
        return;
    }

    RoutesV5Context::AppPairStatusResult st;
    std::string err;
    if (!ctx.app_pair_get(pair_id, st, err)) {
        reply_json(res, 200, json{
            {"ok", true},
            {"state", "missing"},
            {"pair_id", pair_id}
        }.dump());
        return;
    }

    if (st.fingerprint_hex != fp_hex) {
        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", "pairing belongs to another user"}
        }.dump());
        return;
    }

    if (st.expires_at > 0 && now > st.expires_at) {
        reply_json(res, 200, json{
            {"ok", true},
            {"state", "expired"},
            {"pair_id", pair_id},
            {"issued_at", st.issued_at},
            {"expires_at", st.expires_at},
            {"now", now}
        }.dump());
        return;
    }

    if (st.consumed) {
        reply_json(res, 200, json{
            {"ok", true},
            {"state", "consumed"},
            {"pair_id", pair_id},
            {"issued_at", st.issued_at},
            {"expires_at", st.expires_at},
            {"consumed_at", st.consumed_at},
            {"device_id", st.consumed_device_id},
            {"now", now}
        }.dump());
        return;
    }

    reply_json(res, 200, json{
        {"ok", true},
        {"state", "pending"},
        {"pair_id", pair_id},
        {"issued_at", st.issued_at},
        {"expires_at", st.expires_at},
        {"now", now}
    }.dump());
});

// ---- POST /api/v5/app_pair/consume {pair_token, device_name?, platform?, app_version?} ----
srv.Post("/api/v5/app_pair/consume", [&](const httplib::Request& req, httplib::Response& res) {
    json j;
    std::string jerr;
    if (!parse_json_body(req, j, jerr)) {
        audit_v5_req(ctx, "v5.app_pair_consume_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["reason"] = jerr.empty() ? "bad_json" : jerr;
        });

        reply_json(res, 400, json{{"ok", false}, {"error", "bad_request"}, {"message", jerr}}.dump());
        return;
    }

    const std::string pair_token_raw = v5_json_string_or_empty(j, "pair_token");
    const std::string device_name_raw = v5_json_string_or_empty(j, "device_name");
    const std::string platform_raw = v5_json_string_or_empty(j, "platform");
    const std::string app_version_raw = v5_json_string_or_empty(j, "app_version");
    const std::string device_model_raw = v5_json_string_or_empty(j, "device_model");
    const std::string device_manufacturer_raw = v5_json_string_or_empty(j, "device_manufacturer");
    const std::string os_version_raw = v5_json_string_or_empty(j, "os_version");

    std::string pair_token;
    std::string device_name;
    std::string platform;
    std::string app_version;
    std::string device_model;
    std::string device_manufacturer;
    std::string os_version;
    std::string meta_err;

    if (!sanitize_app_pair_token(pair_token_raw, pair_token, meta_err) ||
        !sanitize_app_pair_text_field("device_name", device_name_raw, 96, device_name, meta_err) ||
        !sanitize_app_pair_platform(platform_raw, platform, meta_err) ||
        !sanitize_app_pair_text_field("app_version", app_version_raw, 48, app_version, meta_err) ||
        !sanitize_app_pair_text_field("device_model", device_model_raw, 96, device_model, meta_err) ||
        !sanitize_app_pair_text_field("device_manufacturer", device_manufacturer_raw, 96, device_manufacturer, meta_err) ||
        !sanitize_app_pair_text_field("os_version", os_version_raw, 96, os_version, meta_err)) {
        audit_v5_req(ctx, "v5.app_pair_consume_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["reason"] = meta_err.empty() ? "invalid_pairing_metadata" : meta_err;
        });

        reply_json(res, 400, json{
            {"ok", false},
            {"error", "bad_request"},
            {"message", meta_err.empty() ? "invalid pairing metadata" : meta_err}
        }.dump());
        return;
    }

    if (!ctx.app_pair_consume || !ctx.consume_app_mint) {
        audit_v5_req(ctx, "v5.app_pair_consume_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["reason"] = "pair_consume_not_configured";
        });

        reply_json(res, 500, json{{"ok", false}, {"error", "server_error"}, {"message", "pair consume not configured"}}.dump());
        return;
    }

    std::string pair_id, fingerprint_hex, role;
    std::string cerr;
    if (!ctx.app_pair_consume(pair_token, pair_id, fingerprint_hex, role, cerr)) {
        audit_v5_req(ctx, "v5.app_pair_consume_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["reason"] = cerr.empty() ? "pair_consume_failed" : cerr;
            if (!pair_token.empty()) f["pair_token_sha256"] = sha256_hex(pair_token);
            if (!platform.empty()) f["platform"] = platform;
            if (!app_version.empty()) f["app_version"] = app_version;
        });

        reply_json(res, 409, json{
            {"ok", false},
            {"error", "not_allowed"},
            {"message", cerr.empty() ? "pair consume failed" : cerr}
        }.dump());
        return;
    }

    RoutesV5Context::ConsumeAppResult out;
    std::string merr;
    const std::string client_ip = ctx.client_ip ? ctx.client_ip(req) : req.remote_addr;

    if (!ctx.consume_app_mint(fingerprint_hex,
                          device_name,
                          platform,
                          app_version,
                          device_model,
                          device_manufacturer,
                          os_version,
                          client_ip,
                          out,
                          merr)) {
        std::string rollback_err;
        const bool rolled_back =
            ctx.app_pair_rollback_consumed
                ? ctx.app_pair_rollback_consumed(pair_id, rollback_err)
                : false;

        audit_v5_req(ctx, "v5.app_pair_consume_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
            f["reason"] = merr.empty() ? "pair_mint_denied" : merr;
            f["pair_id"] = pair_id;
            f["fingerprint"] = fingerprint_hex;
            f["role"] = role;
            f["pair_rollback"] = rolled_back ? "ok" : "fail";
            if (!rollback_err.empty()) f["pair_rollback_err"] = rollback_err;
            if (!platform.empty()) f["platform"] = platform;
            if (!device_name.empty()) f["device_name"] = device_name;
            if (!app_version.empty()) f["app_version"] = app_version;
            if (!device_model.empty()) f["device_model"] = device_model;
            if (!device_manufacturer.empty()) f["device_manufacturer"] = device_manufacturer;
            if (!os_version.empty()) f["os_version"] = os_version;
            if (!client_ip.empty()) f["ip"] = client_ip;
        });

        reply_json(res, 403, json{
            {"ok", false},
            {"error", "forbidden"},
            {"message", merr.empty() ? "pair mint denied" : merr}
        }.dump());
        return;
    }
    if (ctx.app_pair_mark_consumed_device) {
        std::string derr;
        if (!ctx.app_pair_mark_consumed_device(pair_id, out.device_id, derr)) {
            audit_v5_req(ctx, "v5.app_pair_consume_mark_device_fail", "fail", req, [&](std::map<std::string,std::string>& f) {
                f["reason"] = derr.empty() ? "mark_consumed_device_failed" : derr;
                f["pair_id"] = pair_id;
                f["device_id"] = out.device_id;
                f["fingerprint"] = out.fingerprint_hex.empty() ? fingerprint_hex : out.fingerprint_hex;
                f["role"] = out.role.empty() ? role : out.role;
            });
        }
    }

    audit_v5_req(ctx, "v5.app_pair_consume_ok", "ok", req, [&](std::map<std::string,std::string>& f) {
        f["pair_id"] = pair_id;
        f["device_id"] = out.device_id;
        f["fingerprint"] = out.fingerprint_hex.empty() ? fingerprint_hex : out.fingerprint_hex;
        f["role"] = out.role.empty() ? role : out.role;
        if (!platform.empty()) f["platform"] = platform;
        if (!device_name.empty()) f["device_name"] = device_name;
        if (!app_version.empty()) f["app_version"] = app_version;
        if (!device_model.empty()) f["device_model"] = device_model;
        if (!device_manufacturer.empty()) f["device_manufacturer"] = device_manufacturer;
        if (!os_version.empty()) f["os_version"] = os_version;
        if (!client_ip.empty()) f["ip"] = client_ip;
    });

    const long now = ctx.now_epoch ? ctx.now_epoch() : 0;
    reply_json(res, 200, json{
        {"ok", true},
        {"token_type", "Bearer"},
        {"access_token", out.access_token},
        {"expires_in", (out.access_exp > now ? out.access_exp - now : 0)},
        {"refresh_token", out.refresh_token},
        {"refresh_expires_in", (out.refresh_exp > now ? out.refresh_exp - now : 0)},
        {"device_id", out.device_id},
        {"fingerprint_hex", out.fingerprint_hex.empty() ? fingerprint_hex : out.fingerprint_hex},
        {"role", out.role.empty() ? role : out.role}
    }.dump());
});
}


