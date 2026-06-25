#pragma once

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace pqnas {

struct ServiceNotice {
    std::string id;
    std::string title;
    std::string body;
    std::string kind;
    std::string severity;

    bool pinned = false;
    bool enabled = true;

    std::int64_t starts_at = 0;
    std::int64_t ends_at = 0;
    std::int64_t created_at = 0;
    std::int64_t updated_at = 0;
};

class ServiceNoticesStore {
public:
    explicit ServiceNoticesStore(std::filesystem::path path);

    bool list_all(std::vector<ServiceNotice>* out, std::string* err) const;
    bool list_active(std::int64_t now_epoch,
                     std::size_t limit,
                     std::vector<ServiceNotice>* out,
                     std::string* err) const;

    bool upsert(ServiceNotice notice, std::string* err);
    bool erase(const std::string& id, bool* removed, std::string* err);

    static nlohmann::json notice_to_json(const ServiceNotice& notice);
    static ServiceNotice notice_from_json(const nlohmann::json& in);
    static bool normalize_for_save(ServiceNotice* notice, std::string* err);

private:
    bool load_locked(std::vector<ServiceNotice>* out, std::string* err) const;
    bool save_locked(const std::vector<ServiceNotice>& notices, std::string* err);

    std::filesystem::path path_;
    mutable std::mutex mu_;
};

} // namespace pqnas
