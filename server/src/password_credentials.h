#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace pqnas {

struct PasswordCredentialRec {
    std::string login;          // normalized login/email
    std::string fingerprint;    // internal PQ-NAS fingerprint hex
    std::string password_hash;  // libsodium crypto_pwhash string
    bool enabled = true;
    bool temporary = false;       // true for generated external workspace invite credentials
    long expires_at_epoch = 0;    // 0 = no credential-level expiry
    std::string created_at;
    std::string updated_at;
};

class PasswordCredentials {
public:
    // Missing file means "empty credential store".
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    std::optional<PasswordCredentialRec> get(const std::string& normalized_login) const;
    bool upsert(const PasswordCredentialRec& rec);
    bool erase(const std::string& normalized_login);

    bool verify_password(const std::string& normalized_login,
                         const std::string& password,
                         PasswordCredentialRec* out = nullptr) const;

    static std::string normalize_login(const std::string& raw);
    static bool hash_password(const std::string& password, std::string& out_hash);

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, PasswordCredentialRec> by_login_;
};

} // namespace pqnas
