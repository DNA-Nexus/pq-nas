#include "service_notices.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <system_error>

namespace pqnas {
namespace {

std::string trim_copy_local(const std::string& in) {
    std::size_t a = 0;
    while (a < in.size() && std::isspace(static_cast<unsigned char>(in[a]))) ++a;

    std::size_t b = in.size();
    while (b > a && std::isspace(static_cast<unsigned char>(in[b - 1]))) --b;

    return in.substr(a, b - a);
}

std::string json_string_local(const nlohmann::json& in,
                              const char* key,
                              const std::string& fallback = std::string()) {
    if (!in.is_object() || !in.contains(key)) return fallback;
    const auto& v = in.at(key);
    if (!v.is_string()) return fallback;
    return v.get<std::string>();
}

bool json_bool_local(const nlohmann::json& in,
                     const char* key,
                     bool fallback) {
    if (!in.is_object() || !in.contains(key)) return fallback;
    const auto& v = in.at(key);
    if (v.is_boolean()) return v.get<bool>();
    return fallback;
}

std::int64_t json_i64_local(const nlohmann::json& in,
                            const char* key,
                            std::int64_t fallback = 0) {
    if (!in.is_object() || !in.contains(key)) return fallback;
    const auto& v = in.at(key);

    try {
        if (v.is_number_integer()) return v.get<std::int64_t>();
        if (v.is_number_unsigned()) {
            const auto u = v.get<std::uint64_t>();
            if (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                return fallback;
            }
            return static_cast<std::int64_t>(u);
        }
        if (v.is_string()) {
            const std::string s = trim_copy_local(v.get<std::string>());
            if (s.empty()) return fallback;
            size_t idx = 0;
            const long long parsed = std::stoll(s, &idx, 10);
            if (idx != s.size()) return fallback;
            return static_cast<std::int64_t>(parsed);
        }
    } catch (...) {
        return fallback;
    }

    return fallback;
}

bool id_is_safe_local(const std::string& id) {
    if (id.empty() || id.size() > 80) return false;

    for (unsigned char c : id) {
        const bool ok =
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') ||
            c == '_' ||
            c == '-';
        if (!ok) return false;
    }

    return true;
}

bool value_in_local(const std::string& value,
                    const std::vector<std::string>& allowed) {
    return std::find(allowed.begin(), allowed.end(), value) != allowed.end();
}

std::string random_suffix_local() {
    std::random_device rd;
    const std::uint64_t a = static_cast<std::uint64_t>(rd());
    const std::uint64_t b = static_cast<std::uint64_t>(rd());
    const std::uint64_t v = (a << 32) ^ b;

    std::ostringstream oss;
    oss << std::hex << std::setw(16) << std::setfill('0') << v;
    return oss.str();
}

int severity_rank_local(const std::string& severity) {
    if (severity == "critical") return 3;
    if (severity == "important") return 2;
    return 1;
}

bool active_at_local(const ServiceNotice& n, std::int64_t now_epoch) {
    if (!n.enabled) return false;
    if (n.starts_at > 0 && n.starts_at > now_epoch) return false;
    if (n.ends_at > 0 && n.ends_at < now_epoch) return false;
    return true;
}

} // namespace

ServiceNoticesStore::ServiceNoticesStore(std::filesystem::path path)
    : path_(std::move(path)) {}

nlohmann::json ServiceNoticesStore::notice_to_json(const ServiceNotice& notice) {
    return nlohmann::json{
        {"id", notice.id},
        {"title", notice.title},
        {"body", notice.body},
        {"kind", notice.kind},
        {"severity", notice.severity},
        {"pinned", notice.pinned},
        {"enabled", notice.enabled},
        {"starts_at", notice.starts_at},
        {"ends_at", notice.ends_at},
        {"created_at", notice.created_at},
        {"updated_at", notice.updated_at}
    };
}

ServiceNotice ServiceNoticesStore::notice_from_json(const nlohmann::json& in) {
    ServiceNotice notice;
    notice.id = trim_copy_local(json_string_local(in, "id"));
    notice.title = trim_copy_local(json_string_local(in, "title"));
    notice.body = trim_copy_local(json_string_local(in, "body"));
    notice.kind = trim_copy_local(json_string_local(in, "kind", "notice"));
    notice.severity = trim_copy_local(json_string_local(in, "severity", "info"));
    notice.pinned = json_bool_local(in, "pinned", false);
    notice.enabled = json_bool_local(in, "enabled", true);
    notice.starts_at = json_i64_local(in, "starts_at", 0);
    notice.ends_at = json_i64_local(in, "ends_at", 0);
    notice.created_at = json_i64_local(in, "created_at", 0);
    notice.updated_at = json_i64_local(in, "updated_at", 0);
    return notice;
}

bool ServiceNoticesStore::normalize_for_save(ServiceNotice* notice, std::string* err) {
    if (err) err->clear();
    if (!notice) {
        if (err) *err = "null notice";
        return false;
    }

    notice->id = trim_copy_local(notice->id);
    notice->title = trim_copy_local(notice->title);
    notice->body = trim_copy_local(notice->body);
    notice->kind = trim_copy_local(notice->kind);
    notice->severity = trim_copy_local(notice->severity);

    if (!id_is_safe_local(notice->id)) {
        if (err) *err = "invalid id";
        return false;
    }

    if (notice->title.empty()) {
        if (err) *err = "title required";
        return false;
    }

    if (notice->title.size() > 160) {
        if (err) *err = "title too long";
        return false;
    }

    if (notice->body.size() > 4000) {
        if (err) *err = "body too long";
        return false;
    }

    if (notice->kind.empty()) notice->kind = "notice";
    if (!value_in_local(notice->kind, {"notice", "maintenance", "outage", "update", "service"})) {
        if (err) *err = "invalid kind";
        return false;
    }

    if (notice->severity.empty()) notice->severity = "info";
    if (!value_in_local(notice->severity, {"info", "important", "critical"})) {
        if (err) *err = "invalid severity";
        return false;
    }

    if (notice->starts_at < 0) notice->starts_at = 0;
    if (notice->ends_at < 0) notice->ends_at = 0;

    if (notice->starts_at > 0 && notice->ends_at > 0 && notice->ends_at < notice->starts_at) {
        if (err) *err = "ends_at before starts_at";
        return false;
    }

    return true;
}

bool ServiceNoticesStore::load_locked(std::vector<ServiceNotice>* out, std::string* err) const {
    if (err) err->clear();
    if (!out) {
        if (err) *err = "null out";
        return false;
    }

    out->clear();

    std::error_code ec;
    if (!std::filesystem::exists(path_, ec)) {
        return true;
    }

    if (ec) {
        if (err) *err = "stat failed: " + ec.message();
        return false;
    }

    std::ifstream f(path_, std::ios::binary);
    if (!f.good()) {
        if (err) *err = "open failed";
        return false;
    }

    const std::string body((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());

    nlohmann::json root = nlohmann::json::parse(body, nullptr, false);
    if (root.is_discarded()) {
        if (err) *err = "invalid json";
        return false;
    }

    nlohmann::json arr = nlohmann::json::array();
    if (root.is_array()) {
        arr = root;
    } else if (root.is_object() && root.contains("notices") && root["notices"].is_array()) {
        arr = root["notices"];
    } else {
        if (err) *err = "invalid service notices root";
        return false;
    }

    for (const auto& item : arr) {
        if (!item.is_object()) continue;

        ServiceNotice notice = notice_from_json(item);
        std::string nerr;
        if (!normalize_for_save(&notice, &nerr)) {
            continue;
        }

        out->push_back(std::move(notice));
    }

    std::stable_sort(out->begin(), out->end(), [](const ServiceNotice& a, const ServiceNotice& b) {
        return a.updated_at > b.updated_at;
    });

    return true;
}

bool ServiceNoticesStore::save_locked(const std::vector<ServiceNotice>& notices, std::string* err) {
    if (err) err->clear();

    std::error_code ec;
    std::filesystem::create_directories(path_.parent_path(), ec);
    if (ec) {
        if (err) *err = "create_directories failed: " + ec.message();
        return false;
    }

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& notice : notices) {
        arr.push_back(notice_to_json(notice));
    }

    nlohmann::json root = {
        {"version", 1},
        {"notices", arr}
    };

    const std::filesystem::path tmp =
        path_.parent_path() / (path_.filename().string() + ".tmp." + random_suffix_local());

    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f.good()) {
            if (err) *err = "open tmp failed";
            return false;
        }

        const std::string body = root.dump(2);
        f.write(body.data(), static_cast<std::streamsize>(body.size()));
        f.put('\n');
        f.flush();

        if (!f.good()) {
            std::filesystem::remove(tmp, ec);
            if (err) *err = "write tmp failed";
            return false;
        }
    }

    std::filesystem::rename(tmp, path_, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        if (err) *err = "rename failed: " + ec.message();
        return false;
    }

    return true;
}

bool ServiceNoticesStore::list_all(std::vector<ServiceNotice>* out, std::string* err) const {
    std::lock_guard<std::mutex> lk(mu_);
    return load_locked(out, err);
}

bool ServiceNoticesStore::list_active(std::int64_t now_epoch,
                                      std::size_t limit,
                                      std::vector<ServiceNotice>* out,
                                      std::string* err) const {
    if (err) err->clear();
    if (!out) {
        if (err) *err = "null out";
        return false;
    }

    std::vector<ServiceNotice> all;
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!load_locked(&all, err)) return false;
    }

    out->clear();
    for (const auto& notice : all) {
        if (active_at_local(notice, now_epoch)) out->push_back(notice);
    }

    std::stable_sort(out->begin(), out->end(), [](const ServiceNotice& a, const ServiceNotice& b) {
        if (a.pinned != b.pinned) return a.pinned && !b.pinned;

        const int ar = severity_rank_local(a.severity);
        const int br = severity_rank_local(b.severity);
        if (ar != br) return ar > br;

        return a.updated_at > b.updated_at;
    });

    if (limit > 0 && out->size() > limit) {
        out->resize(limit);
    }

    return true;
}

bool ServiceNoticesStore::upsert(ServiceNotice notice, std::string* err) {
    if (err) err->clear();

    std::string nerr;
    if (!normalize_for_save(&notice, &nerr)) {
        if (err) *err = nerr;
        return false;
    }

    std::lock_guard<std::mutex> lk(mu_);

    std::vector<ServiceNotice> notices;
    if (!load_locked(&notices, err)) return false;

    for (auto& existing : notices) {
        if (existing.id == notice.id) {
            if (notice.created_at <= 0) notice.created_at = existing.created_at;
            existing = notice;
            return save_locked(notices, err);
        }
    }

    notices.push_back(std::move(notice));
    return save_locked(notices, err);
}

bool ServiceNoticesStore::erase(const std::string& id, bool* removed, std::string* err) {
    if (removed) *removed = false;
    if (err) err->clear();

    const std::string clean_id = trim_copy_local(id);
    if (!id_is_safe_local(clean_id)) {
        if (err) *err = "invalid id";
        return false;
    }

    std::lock_guard<std::mutex> lk(mu_);

    std::vector<ServiceNotice> notices;
    if (!load_locked(&notices, err)) return false;

    const auto old_size = notices.size();
    notices.erase(
        std::remove_if(notices.begin(), notices.end(), [&](const ServiceNotice& n) {
            return n.id == clean_id;
        }),
        notices.end()
    );

    if (removed) *removed = notices.size() != old_size;
    return save_locked(notices, err);
}

} // namespace pqnas
