#include "workspace_links.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>

namespace pqnas {
namespace {

std::string trim_copy_links(std::string s) {
    auto not_space = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
    s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
    return s;
}

bool starts_with_links(const std::string& s, const std::string& pfx) {
    return s.size() >= pfx.size() && s.compare(0, pfx.size(), pfx) == 0;
}

bool is_hexish_id_char(char c) {
    return (c >= 'a' && c <= 'z') ||
           (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

std::string random_hex_links(std::size_t bytes) {
    static thread_local std::mt19937_64 rng([] {
        std::random_device rd;
        const auto now = static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::seed_seq seed{
            rd(), rd(), rd(), rd(),
            static_cast<unsigned int>(now & 0xffffffffULL),
            static_cast<unsigned int>((now >> 32) & 0xffffffffULL)
        };
        return std::mt19937_64(seed);
    }());

    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes * 2);

    for (std::size_t i = 0; i < bytes; ++i) {
        const unsigned v = static_cast<unsigned>(rng() & 0xffU);
        out.push_back(kHex[(v >> 4) & 0x0f]);
        out.push_back(kHex[v & 0x0f]);
    }

    return out;
}

std::filesystem::path link_path_for_id(const std::filesystem::path& workspace_root,
                                       const std::string& id) {
    return workspace_links_dir(workspace_root) / (id + ".json");
}

} // namespace

std::filesystem::path workspace_links_dir(const std::filesystem::path& workspace_root) {
    return workspace_root / ".pqnas" / "links";
}

bool workspace_link_is_valid_id(const std::string& id) {
    if (!starts_with_links(id, "lnk_")) return false;
    if (id.size() < 12 || id.size() > 80) return false;
    for (char c : id) {
        if (!is_hexish_id_char(c)) return false;
    }
    return true;
}

std::string workspace_link_new_id() {
    return "lnk_" + random_hex_links(16);
}

std::string workspace_link_detect_type(const std::string& raw_url) {
    const std::string url = trim_copy_links(raw_url);

    if (starts_with_links(url, "/")) {
        const std::string low = [&] {
            std::string v = url;
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return v;
        }();

        if ((low.find("photo-gallery") != std::string::npos ||
             low.find("photo_gallery") != std::string::npos ||
             low.find("/photos/") != std::string::npos) &&
            low.find("share") != std::string::npos) {
            return "photo_gallery_share";
        }

        return "internal_link";
    }

    if (starts_with_links(url, "http://") || starts_with_links(url, "https://")) {
        return "web_link";
    }

    return "unknown";
}

bool workspace_link_validate_url(const std::string& raw_url, std::string* err) {
    const std::string url = trim_copy_links(raw_url);

    if (url.empty()) {
        if (err) *err = "url is required";
        return false;
    }

    if (url.size() > 2048) {
        if (err) *err = "url is too long";
        return false;
    }

    if (url.find('\0') != std::string::npos ||
        url.find('\r') != std::string::npos ||
        url.find('\n') != std::string::npos) {
        if (err) *err = "url contains invalid control characters";
        return false;
    }

    if (starts_with_links(url, "/")) {
        if (starts_with_links(url, "//")) {
            if (err) *err = "protocol-relative urls are not allowed";
            return false;
        }

        if (url.find('\\') != std::string::npos) {
            if (err) *err = "backslashes are not allowed in internal urls";
            return false;
        }

        return true;
    }

    if (starts_with_links(url, "http://") || starts_with_links(url, "https://")) {
        return true;
    }

    if (err) *err = "only http, https, and internal /path urls are allowed";
    return false;
}

json workspace_link_to_json(const WorkspaceLinkRec& rec) {
    return json{
        {"type", "pqnas_link"},
        {"version", 1},
        {"id", rec.id},
        {"parent_path", rec.parent_path},
        {"name", rec.name},
        {"url", rec.url},
        {"detected_type", rec.detected_type},
        {"created_by_fp", rec.created_by_fp},
        {"created_at_epoch", rec.created_at_epoch},
        {"updated_at_epoch", rec.updated_at_epoch}
    };
}

bool workspace_link_from_json(const json& j, WorkspaceLinkRec* out, std::string* err) {
    if (!out) {
        if (err) *err = "missing output";
        return false;
    }

    if (!j.is_object()) {
        if (err) *err = "link json is not an object";
        return false;
    }

    if (j.value("type", "") != "pqnas_link") {
        if (err) *err = "not a pqnas link";
        return false;
    }

    WorkspaceLinkRec rec;
    rec.id = trim_copy_links(j.value("id", ""));
    rec.parent_path = trim_copy_links(j.value("parent_path", ""));
    rec.name = trim_copy_links(j.value("name", ""));
    rec.url = trim_copy_links(j.value("url", ""));
    rec.detected_type = trim_copy_links(j.value("detected_type", ""));
    rec.created_by_fp = trim_copy_links(j.value("created_by_fp", ""));
    rec.created_at_epoch = j.value("created_at_epoch", static_cast<std::int64_t>(0));
    rec.updated_at_epoch = j.value("updated_at_epoch", static_cast<std::int64_t>(0));

    if (!workspace_link_is_valid_id(rec.id)) {
        if (err) *err = "invalid link id";
        return false;
    }

    if (rec.name.empty()) {
        if (err) *err = "link name is required";
        return false;
    }

    if (rec.name.size() > 160) {
        if (err) *err = "link name is too long";
        return false;
    }

    std::string url_err;
    if (!workspace_link_validate_url(rec.url, &url_err)) {
        if (err) *err = url_err;
        return false;
    }

    if (rec.detected_type.empty()) {
        rec.detected_type = workspace_link_detect_type(rec.url);
    }

    *out = std::move(rec);
    return true;
}

bool workspace_links_load_all(const std::filesystem::path& workspace_root,
                              std::vector<WorkspaceLinkRec>* out,
                              std::string* err) {
    if (!out) {
        if (err) *err = "missing output";
        return false;
    }

    out->clear();

    const auto dir = workspace_links_dir(workspace_root);
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) {
        return true;
    }

    if (ec) {
        if (err) *err = "failed to inspect links directory: " + ec.message();
        return false;
    }

    auto st = std::filesystem::symlink_status(dir, ec);
    if (ec || !std::filesystem::is_directory(st) || std::filesystem::is_symlink(st)) {
        if (err) *err = "links directory is invalid";
        return false;
    }

    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec2;
        const auto p = it->path();
        const auto st2 = it->symlink_status(ec2);
        if (ec2 || std::filesystem::is_symlink(st2) || !std::filesystem::is_regular_file(st2)) {
            continue;
        }

        if (p.extension() != ".json") continue;

        const auto sz = it->file_size(ec2);
        if (ec2 || sz > 65536) continue;

        std::ifstream in(p);
        if (!in) continue;

        json j;
        try {
            in >> j;
        } catch (...) {
            continue;
        }

        WorkspaceLinkRec rec;
        std::string perr;
        if (workspace_link_from_json(j, &rec, &perr)) {
            out->push_back(std::move(rec));
        }
    }

    if (ec) {
        if (err) *err = "failed to read links directory: " + ec.message();
        return false;
    }

    return true;
}

bool workspace_links_load_one(const std::filesystem::path& workspace_root,
                              const std::string& id,
                              WorkspaceLinkRec* out,
                              std::string* err) {
    if (!workspace_link_is_valid_id(id)) {
        if (err) *err = "invalid link id";
        return false;
    }

    const auto p = link_path_for_id(workspace_root, id);

    std::error_code ec;
    auto st = std::filesystem::symlink_status(p, ec);
    if (ec || !std::filesystem::exists(st)) {
        if (err) *err = "link not found";
        return false;
    }

    if (std::filesystem::is_symlink(st) || !std::filesystem::is_regular_file(st)) {
        if (err) *err = "invalid link file";
        return false;
    }

    const auto sz = std::filesystem::file_size(p, ec);
    if (ec || sz > 65536) {
        if (err) *err = "invalid link file size";
        return false;
    }

    std::ifstream in(p);
    if (!in) {
        if (err) *err = "failed to open link";
        return false;
    }

    json j;
    try {
        in >> j;
    } catch (...) {
        if (err) *err = "failed to parse link";
        return false;
    }

    return workspace_link_from_json(j, out, err);
}

bool workspace_links_save_one(const std::filesystem::path& workspace_root,
                              const WorkspaceLinkRec& rec,
                              bool fail_if_exists,
                              std::string* err) {
    if (!workspace_link_is_valid_id(rec.id)) {
        if (err) *err = "invalid link id";
        return false;
    }

    std::string rec_err;
    WorkspaceLinkRec checked;
    if (!workspace_link_from_json(workspace_link_to_json(rec), &checked, &rec_err)) {
        if (err) *err = rec_err;
        return false;
    }

    const auto dir = workspace_links_dir(workspace_root);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        if (err) *err = "failed to create links directory: " + ec.message();
        return false;
    }

    const auto target = link_path_for_id(workspace_root, rec.id);
    if (fail_if_exists && std::filesystem::exists(target, ec)) {
        if (err) *err = "link already exists";
        return false;
    }

    const auto tmp = target.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        if (!out) {
            if (err) *err = "failed to write link temp file";
            return false;
        }

        out << workspace_link_to_json(checked).dump(2) << "\n";
        if (!out) {
            if (err) *err = "failed to write link file";
            return false;
        }
    }

    std::filesystem::rename(tmp, target, ec);
    if (ec) {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);
        if (err) *err = "failed to commit link file: " + ec.message();
        return false;
    }

    return true;
}

bool workspace_links_delete_one(const std::filesystem::path& workspace_root,
                                const std::string& id,
                                std::string* err) {
    if (!workspace_link_is_valid_id(id)) {
        if (err) *err = "invalid link id";
        return false;
    }

    const auto p = link_path_for_id(workspace_root, id);

    std::error_code ec;
    auto st = std::filesystem::symlink_status(p, ec);
    if (ec || !std::filesystem::exists(st)) {
        if (err) *err = "link not found";
        return false;
    }

    if (std::filesystem::is_symlink(st) || !std::filesystem::is_regular_file(st)) {
        if (err) *err = "invalid link file";
        return false;
    }

    std::filesystem::remove(p, ec);
    if (ec) {
        if (err) *err = "failed to delete link: " + ec.message();
        return false;
    }

    return true;
}

} // namespace pqnas
