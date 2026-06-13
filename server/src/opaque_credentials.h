#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace pqnas {

struct OpaqueCredentialRec {
    std::string login;                     // normalized login/email
    std::string fingerprint;               // internal PQ-NAS fingerprint hex
    std::string opaque_password_file_b64;  // serialized OPAQUE server registration/password file
    std::string opaque_suite;              // explicit suite identifier for future migrations
    bool enabled = true;
    bool temporary = false;
    std::string created_at;
    std::string updated_at;
};

class OpaqueCredentials {
public:
    // Missing file means "empty credential store".
    //
    // This class is storage/parsing only. It does not implement OPAQUE crypto,
    // does not read users.json, and does not mint pqnas_session cookies.
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    std::optional<OpaqueCredentialRec> get(const std::string& normalized_login) const;
    bool upsert(const OpaqueCredentialRec& rec);
    bool erase(const std::string& normalized_login);
    std::size_t size() const;

    static std::string normalize_login(const std::string& raw);

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, OpaqueCredentialRec> by_login_;
};

} // namespace pqnas
