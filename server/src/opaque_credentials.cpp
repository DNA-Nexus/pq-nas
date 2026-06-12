#include "opaque_credentials.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>
#include <unistd.h>

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace pqnas {
namespace {

static std::string trim_ascii_copy(const std::string& s) {
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

static bool json_bool_or_default(const json& j, const char* key, bool def) {
    if (!j.is_object() || !j.contains(key)) return def;
    const auto& v = j.at(key);
    if (v.is_boolean()) return v.get<bool>();
    return def;
}

static bool contains_forbidden_password_fallback_field(const json& j) {
    if (!j.is_object()) return false;

    // OPAQUE records must not contain plaintext passwords or classic password
    // hash fallbacks. A malformed account containing these fields makes the
    // whole store fail to load so it cannot silently downgrade security.
    return j.contains("password") ||
           j.contains("plaintext_password") ||
           j.contains("password_hash") ||
           j.contains("classic_password_hash") ||
           j.contains("argon2id_hash");
}

} // namespace

std::string OpaqueCredentials::normalize_login(const std::string& raw) {
    std::string s = trim_ascii_copy(raw);
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool OpaqueCredentials::load(const std::string& path) {
    std::lock_guard<std::mutex> lk(mu_);
    by_login_.clear();

    std::ifstream f(path);
    if (!f.good()) {
        return true; // missing credentials file = empty store
    }

    json j;
    try {
        f >> j;
    } catch (...) {
        return false;
    }

    if (!j.is_object()) return false;

    if (j.contains("version")) {
        const auto& version = j.at("version");
        if (!version.is_number_integer() || version.get<int>() != 1) {
            return false;
        }
    }

    const auto it_accounts = j.find("accounts");
    if (it_accounts == j.end()) return true;
    if (!it_accounts->is_array()) return false;

    for (const auto& it : *it_accounts) {
        if (!it.is_object()) continue;

        if (contains_forbidden_password_fallback_field(it)) {
            return false;
        }

        OpaqueCredentialRec rec;
        rec.login = normalize_login(it.value("login", ""));
        rec.fingerprint = it.value("fingerprint", "");
        rec.opaque_password_file_b64 = it.value("opaque_password_file_b64", "");
        rec.opaque_suite = it.value("opaque_suite", "");
        rec.enabled = json_bool_or_default(it, "enabled", true);
        rec.temporary = json_bool_or_default(it, "temporary", false);
        rec.created_at = it.value("created_at", "");
        rec.updated_at = it.value("updated_at", "");

        if (rec.login.empty()) continue;
        if (rec.fingerprint.empty()) continue;
        if (rec.opaque_password_file_b64.empty()) continue;
        if (rec.opaque_suite.empty()) continue;

        by_login_[rec.login] = rec;
    }

    return true;
}

bool OpaqueCredentials::save(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mu_);

    json j;
    j["version"] = 1;
    j["accounts"] = json::array();

    std::vector<std::string> keys;
    keys.reserve(by_login_.size());
    for (const auto& kv : by_login_) {
        keys.push_back(kv.first);
    }
    std::sort(keys.begin(), keys.end());

    for (const auto& login : keys) {
        const auto& rec = by_login_.at(login);
        j["accounts"].push_back(json{
            {"login", rec.login},
            {"fingerprint", rec.fingerprint},
            {"opaque_password_file_b64", rec.opaque_password_file_b64},
            {"opaque_suite", rec.opaque_suite},
            {"enabled", rec.enabled},
            {"temporary", rec.temporary},
            {"created_at", rec.created_at},
            {"updated_at", rec.updated_at}
        });
    }

    std::filesystem::path p(path);
    std::error_code ec;
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path(), ec);
        if (ec) return false;
    }

    std::filesystem::path tmp = p;
    tmp += ".tmp.";
    tmp += std::to_string(::getpid());
    tmp += ".";
    tmp += std::to_string(static_cast<long long>(std::time(nullptr)));

    {
        std::ofstream out(tmp.string(), std::ios::trunc);
        if (!out.good()) return false;
        out << j.dump(2) << "\n";
        out.flush();
        if (!out.good()) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
    }

    std::filesystem::permissions(
        tmp,
        std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace,
        ec
    );

    ec.clear();
    std::filesystem::rename(tmp, p, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        return false;
    }

    return true;
}

std::optional<OpaqueCredentialRec> OpaqueCredentials::get(const std::string& normalized_login) const {
    const std::string login = normalize_login(normalized_login);
    if (login.empty()) return std::nullopt;

    std::lock_guard<std::mutex> lk(mu_);
    const auto it = by_login_.find(login);
    if (it == by_login_.end()) return std::nullopt;
    return it->second;
}

bool OpaqueCredentials::upsert(const OpaqueCredentialRec& in) {
    OpaqueCredentialRec rec = in;
    rec.login = normalize_login(rec.login);

    if (rec.login.empty()) return false;
    if (rec.fingerprint.empty()) return false;
    if (rec.opaque_password_file_b64.empty()) return false;
    if (rec.opaque_suite.empty()) return false;

    std::lock_guard<std::mutex> lk(mu_);
    by_login_[rec.login] = rec;
    return true;
}

bool OpaqueCredentials::erase(const std::string& normalized_login) {
    const std::string login = normalize_login(normalized_login);
    if (login.empty()) return false;

    std::lock_guard<std::mutex> lk(mu_);
    return by_login_.erase(login) > 0;
}

std::size_t OpaqueCredentials::size() const {
    std::lock_guard<std::mutex> lk(mu_);
    return by_login_.size();
}

} // namespace pqnas
