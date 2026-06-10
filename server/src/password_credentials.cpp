#include "password_credentials.h"

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <system_error>
#include <vector>
#include <unistd.h>

#include <nlohmann/json.hpp>
#include <sodium.h>

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

static bool sodium_ready() {
    return sodium_init() >= 0;
}

static bool json_bool_or_default(const json& j, const char* key, bool def) {
    if (!j.is_object() || !j.contains(key)) return def;
    const auto& v = j.at(key);
    if (v.is_boolean()) return v.get<bool>();
    return def;
}

static long json_long_or_default(const json& j, const char* key, long def) {
    if (!j.is_object() || !j.contains(key)) return def;

    try {
        const auto& v = j.at(key);
        if (v.is_number_integer()) return v.get<long>();
        if (v.is_number_unsigned()) {
            const auto u = v.get<unsigned long long>();
            if (u > static_cast<unsigned long long>(std::numeric_limits<long>::max())) {
                return std::numeric_limits<long>::max();
            }
            return static_cast<long>(u);
        }
    } catch (...) {
        return def;
    }

    return def;
}


static bool password_credentials_dummy_verify(const std::string& password) {
    // Timing equalizer for missing/disabled logins.
    //
    // Without this, a missing login returns immediately while an existing login
    // with a wrong password pays the Argon2id verification cost. That creates a
    // login-existence timing oracle.
    static std::once_flag once;
    static std::string dummy_hash;
    static bool dummy_ready = false;

    std::call_once(once, []() {
        char hash[crypto_pwhash_STRBYTES];

        static constexpr char kDummyPassword[] =
            "pqnas-password-credentials-dummy-timing-equalizer";

        if (crypto_pwhash_str_alg(hash,
                                  kDummyPassword,
                                  sizeof(kDummyPassword) - 1,
                                  crypto_pwhash_OPSLIMIT_INTERACTIVE,
                                  crypto_pwhash_MEMLIMIT_INTERACTIVE,
                                  crypto_pwhash_ALG_ARGON2ID13) == 0) {
            dummy_hash = hash;
            dummy_ready = true;
        }

        sodium_memzero(hash, sizeof(hash));
    });

    if (!dummy_ready || dummy_hash.empty()) {
        return false;
    }

    const int rc = crypto_pwhash_str_verify(dummy_hash.c_str(),
                                             password.data(),
                                             password.size());

    // The result is intentionally not used for authentication. This function is
    // only a timing equalizer for missing/disabled accounts, but we still read
    // the return value because libsodium marks it warn_unused_result.
    if (rc == 0) {
        return false;
    }

    return false;
}


} // namespace

std::string PasswordCredentials::normalize_login(const std::string& raw) {
    std::string s = trim_ascii_copy(raw);
    for (char& ch : s) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return s;
}

bool PasswordCredentials::hash_password(const std::string& password, std::string& out_hash) {
    out_hash.clear();

    if (password.empty() || password.size() > 1024) {
        return false;
    }

    if (!sodium_ready()) {
        return false;
    }

    char hash[crypto_pwhash_STRBYTES];

    if (crypto_pwhash_str_alg(hash,
                              password.data(),
                              password.size(),
                              crypto_pwhash_OPSLIMIT_INTERACTIVE,
                              crypto_pwhash_MEMLIMIT_INTERACTIVE,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) {
        sodium_memzero(hash, sizeof(hash));
        return false;
    }

    out_hash = hash;
    sodium_memzero(hash, sizeof(hash));
    return !out_hash.empty();
}

bool PasswordCredentials::load(const std::string& path) {
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

    const auto it_accounts = j.find("accounts");
    if (it_accounts == j.end()) return true;
    if (!it_accounts->is_array()) return false;

    for (const auto& it : *it_accounts) {
        if (!it.is_object()) continue;

        PasswordCredentialRec rec;
        rec.login = normalize_login(it.value("login", ""));
        rec.fingerprint = it.value("fingerprint", "");
        rec.password_hash = it.value("password_hash", "");
        rec.enabled = json_bool_or_default(it, "enabled", true);
        rec.temporary = json_bool_or_default(it, "temporary", false);
        rec.expires_at_epoch = json_long_or_default(it, "expires_at_epoch", 0);
        rec.created_at = it.value("created_at", "");
        rec.updated_at = it.value("updated_at", "");

        if (rec.login.empty()) continue;
        if (rec.fingerprint.empty()) continue;
        if (rec.password_hash.empty()) continue;

        by_login_[rec.login] = rec;
    }

    return true;
}

bool PasswordCredentials::save(const std::string& path) const {
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
            {"password_hash", rec.password_hash},
            {"enabled", rec.enabled},
            {"temporary", rec.temporary},
            {"expires_at_epoch", rec.expires_at_epoch},
            {"created_at", rec.created_at},
            {"updated_at", rec.updated_at}
        });
    }

    std::filesystem::path p(path);
    std::error_code ec;
    std::filesystem::create_directories(p.parent_path(), ec);
    if (ec) return false;

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

std::optional<PasswordCredentialRec> PasswordCredentials::get(const std::string& normalized_login) const {
    const std::string login = normalize_login(normalized_login);
    if (login.empty()) return std::nullopt;

    std::lock_guard<std::mutex> lk(mu_);
    auto it = by_login_.find(login);
    if (it == by_login_.end()) return std::nullopt;
    return it->second;
}

bool PasswordCredentials::upsert(const PasswordCredentialRec& in) {
    PasswordCredentialRec rec = in;
    rec.login = normalize_login(rec.login);

    if (rec.login.empty()) return false;
    if (rec.fingerprint.empty()) return false;
    if (rec.password_hash.empty()) return false;

    std::lock_guard<std::mutex> lk(mu_);
    by_login_[rec.login] = rec;
    return true;
}

bool PasswordCredentials::erase(const std::string& normalized_login) {
    const std::string login = normalize_login(normalized_login);
    if (login.empty()) return false;

    std::lock_guard<std::mutex> lk(mu_);
    return by_login_.erase(login) > 0;
}

bool PasswordCredentials::verify_password(const std::string& normalized_login,
                                          const std::string& password,
                                          PasswordCredentialRec* out) const {
    if (password.empty() || password.size() > 1024) {
        return false;
    }

    if (!sodium_ready()) {
        return false;
    }

    const auto rec_opt = get(normalized_login);
    if (!rec_opt.has_value()) {
        return password_credentials_dummy_verify(password);
    }

    const PasswordCredentialRec& rec = *rec_opt;
    if (!rec.enabled || rec.password_hash.empty()) {
        return password_credentials_dummy_verify(password);
    }

    const int rc = crypto_pwhash_str_verify(rec.password_hash.c_str(),
                                            password.data(),
                                            password.size());
    if (rc != 0) {
        return false;
    }

    if (out) {
        *out = rec;
    }

    return true;
}

} // namespace pqnas
