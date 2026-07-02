#include "storage_pools.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <set>
#include <system_error>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace pqnas {

namespace {

std::string getenv_str(const std::string& key) {
    const char* v = std::getenv(key.c_str());
    return v ? std::string(v) : std::string();
}

bool read_text_file(const std::string& path, std::string* out) {
    if (out) out->clear();

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    if (!f.good() && !f.eof()) return false;

    if (out) *out = std::move(s);
    return true;
}

bool write_text_file_atomic_raw(const std::string& path, const std::string& content) {
    const std::string tmp = path + ".tmp";

    std::ofstream f(tmp, std::ios::binary);
    if (!f) return false;

    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.close();
    if (!f) return false;

    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp);
        return false;
    }
    return true;
}

std::string iso8601_now_fallback() {
    // Minimal fallback only for config migration timestamps if needed.
    // If you already have iso8601_now() globally available, replace this helper
    // with that function call and delete this fallback.
    return "";
}

std::string trim_copy_safe(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower_ascii_copy(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}


std::string json_string_trimmed(const json& j, const char* key) {
    if (!j.is_object() || !j.contains(key) || !j[key].is_string()) return "";
    return trim_copy_safe(j[key].get<std::string>());
}

std::string basename_copy(const std::string& path) {
    if (path.empty()) return "";
    const auto name = std::filesystem::path(path).filename().string();
    if (!name.empty()) return name;

    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

bool is_dev_path(const std::string& s) {
    return s.rfind("/dev/", 0) == 0;
}

bool same_file_best_effort(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty()) return false;

    std::error_code ec;
    const bool eq = std::filesystem::equivalent(a, b, ec);
    return !ec && eq;
}

std::string first_samefile_link_under(const std::string& dir, const std::string& dev) {
    if (!is_dev_path(dev)) return "";

    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec) || ec) return "";

    std::vector<std::string> matches;

    for (const auto& ent : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;

        const std::string p = ent.path().string();
        if (p.empty()) continue;

        if (same_file_best_effort(p, dev)) {
            matches.push_back(p);
        }
    }

    if (matches.empty()) return "";

    std::sort(matches.begin(), matches.end());
    return matches.front();
}

std::string read_first_line_trimmed(const std::string& path) {
    std::ifstream f(path);
    if (!f) return "";

    std::string s;
    std::getline(f, s);
    return trim_copy_safe(s);
}

std::string sys_block_name_from_dev(const std::string& dev) {
    const std::string base = basename_copy(dev);
    if (base.empty()) return "";
    return base;
}

std::string derive_serial_short_from_disk_id(const std::string& disk_id) {
    std::string s = trim_copy_safe(disk_id);
    if (s.empty()) return "";

    // Common USB by-id format:
    // usb-Kingston_DataTraveler_3.0_E0D55EA574D4E9C059E50145-0:0
    if (s.rfind("usb-", 0) == 0) {
        const auto dash = s.rfind("-0:");
        if (dash != std::string::npos) s = s.substr(0, dash);

        const auto us = s.rfind('_');
        if (us != std::string::npos && us + 1 < s.size()) {
            return s.substr(us + 1);
        }
    }

    if (s.rfind("wwn-", 0) == 0 && s.size() > 4) {
        return s.substr(4);
    }

    return "";
}


std::string run_argv_capture_limited(const std::vector<std::string>& argv_s,
                                     std::size_t max_bytes = 65536) {
    std::string out;
    if (argv_s.empty()) return out;

    int pipefd[2] = {-1, -1};
    if (::pipe(pipefd) != 0) return out;

    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipefd[0]);
        ::close(pipefd[1]);
        return out;
    }

    if (pid == 0) {
        ::close(pipefd[0]);
        (void)::dup2(pipefd[1], STDOUT_FILENO);
        ::close(pipefd[1]);

        const int nullfd = ::open("/dev/null", O_WRONLY);
        if (nullfd >= 0) {
            (void)::dup2(nullfd, STDERR_FILENO);
            ::close(nullfd);
        }

        std::vector<char*> argv;
        argv.reserve(argv_s.size() + 1);
        for (const auto& a : argv_s) {
            argv.push_back(const_cast<char*>(a.c_str()));
        }
        argv.push_back(nullptr);

        // Security: execute storage pool probes as argv; device paths never reach a shell.
        ::execv(argv_s[0].c_str(), argv.data());
        _exit(127);
    }

    ::close(pipefd[1]);

    std::array<char, 4096> buf{};
    while (true) {
        const ssize_t n = ::read(pipefd[0], buf.data(), buf.size());
        if (n > 0) {
            if (out.size() < max_bytes) {
                const std::size_t room = max_bytes - out.size();
                const std::size_t take = std::min<std::size_t>(
                    room,
                    static_cast<std::size_t>(n)
                );
                out.append(buf.data(), take);
            }
            continue;
        }
        break;
    }

    ::close(pipefd[0]);

    int st = 0;
    if (::waitpid(pid, &st, 0) < 0) return "";
    if (!WIFEXITED(st) || WEXITSTATUS(st) != 0) return "";

    return out;
}

std::map<std::string, std::string> parse_key_value_lines(const std::string& txt) {
    std::map<std::string, std::string> out;

    std::istringstream is(txt);
    std::string line;

    while (std::getline(is, line)) {
        line = trim_copy_safe(line);
        if (line.empty()) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string k = trim_copy_safe(line.substr(0, eq));
        const std::string v = trim_copy_safe(line.substr(eq + 1));

        if (!k.empty()) out[k] = v;
    }

    return out;
}

std::map<std::string, std::string> udev_properties_for_dev(const std::string& dev) {
    if (!is_dev_path(dev)) return {};

    const std::vector<std::string> argv = {
        "/usr/bin/udevadm",
        "info",
        "--query=property",
        "--name=" + dev
    };

    const std::string out = run_argv_capture_limited(argv);
    if (out.empty()) return {};

    return parse_key_value_lines(out);
}

std::vector<std::string> split_space_list(const std::string& s) {
    std::vector<std::string> out;
    std::istringstream is(s);
    std::string one;

    while (is >> one) {
        one = trim_copy_safe(one);
        if (!one.empty()) out.push_back(one);
    }

    return out;
}

std::string first_devlink_with_prefix(const std::string& devlinks, const std::string& prefix) {
    for (const auto& link : split_space_list(devlinks)) {
        if (link.rfind(prefix, 0) == 0) return link;
    }
    return "";
}

void enrich_identity_from_udev(json* out, const std::string& dev) {
    if (!out || !out->is_object() || !is_dev_path(dev)) return;

    const auto props = udev_properties_for_dev(dev);
    if (props.empty()) return;

    auto get = [&](const char* key) -> std::string {
        auto it = props.find(key);
        if (it == props.end()) return "";
        return trim_copy_safe(it->second);
    };

    const std::string devlinks = get("DEVLINKS");

    if (!out->contains("by_id") || !(*out)["by_id"].is_string() ||
        trim_copy_safe((*out)["by_id"].get<std::string>()).empty()) {
        const std::string by_id = first_devlink_with_prefix(devlinks, "/dev/disk/by-id/");
        if (!by_id.empty()) {
            (*out)["by_id"] = by_id;
            (*out)["disk_id"] = basename_copy(by_id);
        }
    }

    if (!out->contains("by_path") || !(*out)["by_path"].is_string() ||
        trim_copy_safe((*out)["by_path"].get<std::string>()).empty()) {
        const std::string by_path = first_devlink_with_prefix(devlinks, "/dev/disk/by-path/");
        if (!by_path.empty()) (*out)["by_path"] = by_path;
    }

    if (!out->contains("id_serial_short") || !(*out)["id_serial_short"].is_string() ||
        trim_copy_safe((*out)["id_serial_short"].get<std::string>()).empty()) {
        const std::string serial_short = get("ID_SERIAL_SHORT");
        if (!serial_short.empty()) (*out)["id_serial_short"] = serial_short;
    }

    if (!out->contains("model") || !(*out)["model"].is_string() ||
        trim_copy_safe((*out)["model"].get<std::string>()).empty()) {
        std::string model = get("ID_MODEL");
        std::replace(model.begin(), model.end(), '_', ' ');
        if (!model.empty()) (*out)["model"] = model;
    }

    if (!out->contains("disk_id") || !(*out)["disk_id"].is_string() ||
        trim_copy_safe((*out)["disk_id"].get<std::string>()).empty()) {
        const std::string by_id = json_string_trimmed(*out, "by_id");
        if (!by_id.empty()) (*out)["disk_id"] = basename_copy(by_id);
    }
}

void copy_string_identity_if_present(json* dst, const json& src, const char* key, bool overwrite = false) {
    if (!dst || !dst->is_object() || !src.is_object()) return;
    if (!src.contains(key) || !src[key].is_string()) return;

    const std::string v = trim_copy_safe(src[key].get<std::string>());
    if (v.empty()) return;

    if (!overwrite && dst->contains(key) && (*dst)[key].is_string() &&
        !trim_copy_safe((*dst)[key].get<std::string>()).empty()) {
        return;
    }

    (*dst)[key] = v;
}

void copy_integer_identity_if_present(json* dst, const json& src, const char* key, bool overwrite = false) {
    if (!dst || !dst->is_object() || !src.is_object()) return;
    if (!src.contains(key) || !src[key].is_number_integer()) return;

    if (!overwrite && dst->contains(key) && (*dst)[key].is_number_integer()) {
        return;
    }

    (*dst)[key] = src[key];
}

json runtime_identity_for_dev(const std::string& dev_in) {
    const std::string dev = trim_copy_safe(dev_in);
    json out = json::object();

    if (!is_dev_path(dev)) return out;

    out["runtime_dev"] = dev;

    const std::string by_id = first_samefile_link_under("/dev/disk/by-id", dev);
    if (!by_id.empty()) {
        out["by_id"] = by_id;
        out["disk_id"] = basename_copy(by_id);
    }

    const std::string by_path = first_samefile_link_under("/dev/disk/by-path", dev);
    if (!by_path.empty()) {
        out["by_path"] = by_path;
    }

    const std::string block = sys_block_name_from_dev(dev);
    if (!block.empty()) {
        const std::string model = read_first_line_trimmed("/sys/class/block/" + block + "/device/model");
        if (!model.empty()) out["model"] = model;

        const std::string serial = read_first_line_trimmed("/sys/class/block/" + block + "/device/serial");
        if (!serial.empty()) out["id_serial_short"] = serial;
    }

    enrich_identity_from_udev(&out, dev);

    if (!out.contains("id_serial_short")) {
        const std::string disk_id = json_string_trimmed(out, "disk_id");
        const std::string derived = derive_serial_short_from_disk_id(disk_id);
        if (!derived.empty()) out["id_serial_short"] = derived;
    }

    return out;
}

void copy_slot_identity_fields(json* dst, const json& src, bool overwrite = false) {
    copy_string_identity_if_present(dst, src, "disk_id", overwrite);
    copy_string_identity_if_present(dst, src, "by_id", overwrite);
    copy_string_identity_if_present(dst, src, "by_path", overwrite);
    copy_string_identity_if_present(dst, src, "runtime_dev", overwrite);
    copy_string_identity_if_present(dst, src, "id_serial_short", overwrite);
    copy_string_identity_if_present(dst, src, "model", overwrite);
    copy_integer_identity_if_present(dst, src, "btrfs_devid", overwrite);
}

bool identity_string_equal(const json& a, const json& b, const char* key) {
    const std::string av = json_string_trimmed(a, key);
    const std::string bv = json_string_trimmed(b, key);
    return !av.empty() && av == bv;
}

bool identity_integer_equal(const json& a, const json& b, const char* key) {
    if (!a.is_object() || !b.is_object()) return false;
    if (!a.contains(key) || !b.contains(key)) return false;
    if (!a[key].is_number_integer() || !b[key].is_number_integer()) return false;
    return a[key].get<int64_t>() == b[key].get<int64_t>();
}

int find_matching_runtime_member(const json& saved_slot,
                                 const std::string& saved_device,
                                 const std::vector<json>& runtime_members,
                                 const std::set<int>& used_runtime_indexes) {
    auto find_by_string_key = [&](const char* key) -> int {
        for (int i = 0; i < static_cast<int>(runtime_members.size()); ++i) {
            if (used_runtime_indexes.count(i)) continue;
            if (identity_string_equal(saved_slot, runtime_members[static_cast<size_t>(i)], key)) {
                return i;
            }
        }
        return -1;
    };

    int idx = find_by_string_key("by_id");
    if (idx >= 0) return idx;

    idx = find_by_string_key("disk_id");
    if (idx >= 0) return idx;

    idx = find_by_string_key("by_path");
    if (idx >= 0) return idx;

    for (int i = 0; i < static_cast<int>(runtime_members.size()); ++i) {
        if (used_runtime_indexes.count(i)) continue;
        if (identity_integer_equal(saved_slot, runtime_members[static_cast<size_t>(i)], "btrfs_devid")) {
            return i;
        }
    }

    const std::string saved_runtime_dev = json_string_trimmed(saved_slot, "runtime_dev");

    for (int i = 0; i < static_cast<int>(runtime_members.size()); ++i) {
        if (used_runtime_indexes.count(i)) continue;

        const std::string rt = json_string_trimmed(runtime_members[static_cast<size_t>(i)], "runtime_dev");
        if (rt.empty()) continue;

        if (!saved_runtime_dev.empty() && saved_runtime_dev == rt) return i;
        if (!saved_device.empty() && saved_device == rt) return i;
    }

    return -1;
}

std::filesystem::path pools_cfg_path_from_users_path_local(const std::string& users_path) {
    std::string root = getenv_str("PQNAS_STORAGE_ROOT");
    if (root.empty()) root = "/srv/pqnas";

    std::filesystem::path p = std::filesystem::path(root) / "config" / "pools.json";

    std::error_code ec;
    auto st = std::filesystem::status(std::filesystem::path(root) / "config", ec);
    if (!ec && std::filesystem::is_directory(st)) return p;

    return std::filesystem::path(users_path).parent_path() / "pools.json";
}

json make_empty_slot(int index) {
    return json{
        {"index", index},
        {"device", nullptr}
    };
}

json make_slot(int index, const std::string& device) {
    return json{
        {"index", index},
        {"device", device.empty() ? json(nullptr) : json(device)}
    };
}

void normalize_slots_array(json* slots) {
    if (!slots || !slots->is_array()) {
        if (slots) *slots = json::array();
        return;
    }

    json out = json::array();
    std::set<std::string> seen_devices;
    int idx = 0;

    for (auto& s : *slots) {
        std::string dev;

        if (s.is_object() && s.contains("device") && s["device"].is_string()) {
            dev = trim_copy_safe(s["device"].get<std::string>());
        }

        if (!dev.empty() && dev.rfind("/dev/", 0) != 0) {
            dev.clear();
        }

        if (!dev.empty()) {
            if (!seen_devices.insert(dev).second) {
                dev.clear();
            }
        }

        json one = make_slot(idx++, dev);

        if (s.is_object()) {
            auto copy_slot_identity_field = [&](const char* key) {
                if (s.contains(key) && s[key].is_string() && !s[key].get<std::string>().empty()) {
                    one[key] = s[key];
                }
            };

            copy_slot_identity_field("disk_id");
            copy_slot_identity_field("by_id");
            copy_slot_identity_field("by_path");
            copy_slot_identity_field("runtime_dev");
            copy_slot_identity_field("id_serial_short");
            copy_slot_identity_field("model");

            if (s.contains("btrfs_devid") && s["btrfs_devid"].is_number_integer()) {
                one["btrfs_devid"] = s["btrfs_devid"];
            }
        }

        out.push_back(std::move(one));
    }

    *slots = std::move(out);
}

} // namespace

void normalize_pool_entry_v3(json* pool_obj) {
    if (!pool_obj || !pool_obj->is_object()) {
        if (pool_obj) *pool_obj = json::object();
        return;
    }

    if (!pool_obj->contains("pool_id") || !(*pool_obj)["pool_id"].is_string())
        (*pool_obj)["pool_id"] = "";

    if (!pool_obj->contains("display_name") || !(*pool_obj)["display_name"].is_string())
        (*pool_obj)["display_name"] = "";

    if (!pool_obj->contains("created_ts") || !(*pool_obj)["created_ts"].is_string())
        (*pool_obj)["created_ts"] = "";

    if (!pool_obj->contains("managed") || !(*pool_obj)["managed"].is_boolean())
        (*pool_obj)["managed"] = true;

    if (!pool_obj->contains("fs_label") || !(*pool_obj)["fs_label"].is_string())
        (*pool_obj)["fs_label"] = "";

    if (!pool_obj->contains("fs_uuid") || !(*pool_obj)["fs_uuid"].is_string())
        (*pool_obj)["fs_uuid"] = "";

    if (!pool_obj->contains("mode") || !(*pool_obj)["mode"].is_string()) {
        (*pool_obj)["mode"] = "single";
    } else {
        const std::string mode = lower_ascii_copy(trim_copy_safe((*pool_obj)["mode"].get<std::string>()));
        (*pool_obj)["mode"] = (mode == "raid1") ? "raid1" : "single";
    }

    if (!pool_obj->contains("slots") || !(*pool_obj)["slots"].is_array()) {
        (*pool_obj)["slots"] = json::array();
    }

    normalize_slots_array(&(*pool_obj)["slots"]);

    int slot_count = static_cast<int>((*pool_obj)["slots"].size());
    if (pool_obj->contains("slot_count") && (*pool_obj)["slot_count"].is_number_integer()) {
        slot_count = std::max(slot_count, (*pool_obj)["slot_count"].get<int>());
    }

    if (slot_count < 0) slot_count = 0;

    while (static_cast<int>((*pool_obj)["slots"].size()) < slot_count) {
        (*pool_obj)["slots"].push_back(make_empty_slot(static_cast<int>((*pool_obj)["slots"].size())));
    }

    (*pool_obj)["slot_count"] = static_cast<int>((*pool_obj)["slots"].size());
}


void enrich_pool_slots_with_runtime_identity_v3(json* pool_obj) {
    if (!pool_obj || !pool_obj->is_object()) return;

    normalize_pool_entry_v3(pool_obj);

    if (!pool_obj->contains("slots") || !(*pool_obj)["slots"].is_array()) return;

    for (auto& s : (*pool_obj)["slots"]) {
        if (!s.is_object()) continue;

        std::string dev = json_string_trimmed(s, "runtime_dev");
        if (dev.empty()) dev = json_string_trimmed(s, "device");
        if (!is_dev_path(dev)) continue;

        json id = runtime_identity_for_dev(dev);
        if (!id.is_object() || id.empty()) continue;

        copy_slot_identity_fields(&s, id, true);

        const std::string rt = json_string_trimmed(id, "runtime_dev");
        if (!rt.empty()) {
            s["device"] = rt;       // legacy / last runtime path
            s["runtime_dev"] = rt;  // explicit current runtime path
        }
    }

    normalize_pool_entry_v3(pool_obj);
}

void ensure_pools_cfg_shape_v3(json* cfg) {
    if (!cfg || !cfg->is_object()) {
        if (cfg) *cfg = json::object();
        return;
    }

    if (!cfg->contains("names_by_mount") || !(*cfg)["names_by_mount"].is_object()) {
        (*cfg)["names_by_mount"] = json::object();
    }

    if (!cfg->contains("pools") || !(*cfg)["pools"].is_object()) {
        (*cfg)["pools"] = json::object();
    }

    for (auto it = (*cfg)["pools"].begin(); it != (*cfg)["pools"].end(); ++it) {
        normalize_pool_entry_v3(&it.value());
    }

    (*cfg)["version"] = 3;
}

json load_or_init_pools_cfg_v3(const std::string& users_path) {
    const auto cfg_path = pools_cfg_path_from_users_path_local(users_path);

    std::string txt;
    json j;

    if (read_text_file(cfg_path.string(), &txt)) {
        try {
            j = json::parse(txt);
        } catch (...) {
            j = json::object();
        }
    }

    if (!j.is_object()) {
        j = json::object();
    }

    int version = j.value("version", 0);

    // init
    if (version == 0) {
        j["version"] = 3;
        j["names_by_mount"] = json::object();
        j["pools"] = json::object();
        ensure_pools_cfg_shape_v3(&j);
        return j;
    }

    // migrate v1 -> v2-ish shape first
    if (version == 1) {
        json pools = json::object();
        const auto names = j.value("names_by_mount", json::object());

        for (auto it = names.begin(); it != names.end(); ++it) {
            const std::string mount = it.key();
            const std::string display = it.value().is_string() ? it.value().get<std::string>() : "";

            pools[mount] = json{
                {"pool_id", ""},
                {"display_name", display},
                {"created_ts", iso8601_now_fallback()},
                {"managed", false},
                {"fs_label", ""},
                {"fs_uuid", ""},
                {"mode", "single"},
                {"slot_count", 0},
                {"slots", json::array()}
            };
        }

        j.clear();
        j["version"] = 3;
        j["names_by_mount"] = names.is_object() ? names : json::object();
        j["pools"] = pools;
        ensure_pools_cfg_shape_v3(&j);
        (void)write_text_file_atomic_raw(cfg_path.string(), j.dump(2) + "\n");
        return j;
    }

    // migrate v2 -> v3
    if (version == 2) {
        if (!j.contains("names_by_mount") || !j["names_by_mount"].is_object()) {
            j["names_by_mount"] = json::object();
        }
        if (!j.contains("pools") || !j["pools"].is_object()) {
            j["pools"] = json::object();
        }

        for (auto it = j["pools"].begin(); it != j["pools"].end(); ++it) {
            json& p = it.value();
            if (!p.is_object()) p = json::object();

            if (!p.contains("pool_id") || !p["pool_id"].is_string())
                p["pool_id"] = "";

            if (!p.contains("display_name") || !p["display_name"].is_string())
                p["display_name"] = "";

            if (!p.contains("created_ts") || !p["created_ts"].is_string())
                p["created_ts"] = "";

            if (!p.contains("managed") || !p["managed"].is_boolean())
                p["managed"] = false;

            if (!p.contains("fs_label") || !p["fs_label"].is_string())
                p["fs_label"] = "";

            if (!p.contains("fs_uuid") || !p["fs_uuid"].is_string())
                p["fs_uuid"] = "";

            if (!p.contains("mode") || !p["mode"].is_string())
                p["mode"] = "single";

            if (!p.contains("slot_count") || !p["slot_count"].is_number_integer())
                p["slot_count"] = 0;

            if (!p.contains("slots") || !p["slots"].is_array())
                p["slots"] = json::array();
        }

        ensure_pools_cfg_shape_v3(&j);
        (void)write_text_file_atomic_raw(cfg_path.string(), j.dump(2) + "\n");
        return j;
    }

    ensure_pools_cfg_shape_v3(&j);
    return j;
}

bool write_pools_cfg_v3(const std::string& users_path, const json& in_cfg, std::string* err) {
    if (err) err->clear();

    json cfg = in_cfg;
    ensure_pools_cfg_shape_v3(&cfg);

    const auto cfg_path = pools_cfg_path_from_users_path_local(users_path);
    if (!write_text_file_atomic_raw(cfg_path.string(), cfg.dump(2) + "\n")) {
        if (err) *err = "write_text_file_atomic failed for " + cfg_path.string();
        return false;
    }
    return true;
}

std::string pools_display_name_for_mount_v3(const json& cfg, const std::string& mount) {
    if (!cfg.is_object()) return "";

    if (cfg.contains("pools") && cfg["pools"].is_object()) {
        auto it = cfg["pools"].find(mount);
        if (it != cfg["pools"].end() && it->is_object()) {
            const std::string s = trim_copy_safe(it->value("display_name", ""));
            if (!s.empty()) return s;
        }
    }

    if (cfg.contains("names_by_mount") && cfg["names_by_mount"].is_object()) {
        auto it = cfg["names_by_mount"].find(mount);
        if (it != cfg["names_by_mount"].end() && it->is_string()) {
            return trim_copy_safe(it->get<std::string>());
        }
    }

    return "";
}

std::string pool_mode_from_profiles_best_effort(const std::string& profile_data,
                                                const std::string& profile_meta) {
    const std::string d = lower_ascii_copy(trim_copy_safe(profile_data));
    const std::string m = lower_ascii_copy(trim_copy_safe(profile_meta));

    if (d == "raid1" && m == "raid1") return "raid1";
    if (d == "single" || m == "single") return "single";

    return "single";
}

std::vector<std::string> runtime_member_parent_disks_from_show_json(const json& btrfs_show_json) {
    std::vector<std::string> out;
    std::set<std::string> seen;

    if (!btrfs_show_json.is_object()) return out;
    if (!btrfs_show_json.contains("devices") || !btrfs_show_json["devices"].is_array()) return out;

    for (const auto& d : btrfs_show_json["devices"]) {
        if (!d.is_object()) continue;

        const std::string pd = d.value("parent_disk", "");
        const std::string p  = d.value("path", "");

        const std::string chosen = !pd.empty() ? pd : p;
        if (chosen.empty()) continue;

        if (seen.insert(chosen).second) {
            out.push_back(chosen);
        }
    }

    return out;
}

void infer_slots_from_runtime_if_missing(json* cfg_pool,
                                         const std::vector<std::string>& runtime_member_parents) {
    if (!cfg_pool || !cfg_pool->is_object()) return;

    const bool has_slots =
        cfg_pool->contains("slots") &&
        (*cfg_pool)["slots"].is_array() &&
        !(*cfg_pool)["slots"].empty();

    if (has_slots) {
        normalize_pool_entry_v3(cfg_pool);
        return;
    }

    json slots = json::array();
    int idx = 0;
    for (const auto& d : runtime_member_parents) {
        slots.push_back(make_slot(idx++, d));
    }

    if (slots.empty()) {
        slots.push_back(make_empty_slot(0));
    }

    (*cfg_pool)["slots"] = std::move(slots);
    (*cfg_pool)["slot_count"] = static_cast<int>((*cfg_pool)["slots"].size());

    if (!cfg_pool->contains("mode") || !(*cfg_pool)["mode"].is_string()) {
        (*cfg_pool)["mode"] = "single";
    }

    normalize_pool_entry_v3(cfg_pool);
}

json merge_pool_runtime_and_config(const json& cfg_pool,
                                   const json& runtime_pool,
                                   const std::vector<std::string>& runtime_member_parents,
                                   bool busy,
                                   const std::string& busy_lock) {
    json out = json::object();

    const bool has_cfg = cfg_pool.is_object();
    const bool has_rt  = runtime_pool.is_object();

    // ----------------------------
    // Identity / mount
    // ----------------------------
    std::string mount;
    if (has_rt) mount = runtime_pool.value("mount", "");
    if (mount.empty() && has_cfg) mount = cfg_pool.value("mount", "");
    out["mount"] = mount;

    std::string pool_id;
    if (has_cfg) pool_id = cfg_pool.value("pool_id", "");
    if (pool_id.empty() && has_rt) pool_id = runtime_pool.value("pool_id", "");
    out["pool_id"] = pool_id;

    bool managed = has_cfg ? cfg_pool.value("managed", true) : false;
    out["managed"] = managed;

    // ----------------------------
    // Runtime info
    // ----------------------------
	if (has_rt) {
   		out["uuid"] = runtime_pool.value("uuid", "");
   		out["label"] = runtime_pool.value("label", "");
   		out["devices"] = runtime_pool.value("devices", 0);
   		out["profile_data"] = runtime_pool.value("profile_data", "");
   		out["profile_metadata"] = runtime_pool.value("profile_metadata", "");
   		out["size_bytes"] = runtime_pool.value("size_bytes", int64_t{0});
   		out["used_bytes"] = runtime_pool.value("used_bytes", int64_t{0});
   		out["resolved_source"] = runtime_pool.value("resolved_source", "");
   		out["resolved_disk"] = runtime_pool.value("resolved_disk", "");
   		out["fstype"] = "btrfs";
   		out["free_estimated_bytes"] = runtime_pool.value("free_estimated_bytes", int64_t{0});
   		out["usable_total_bytes"] = runtime_pool.value("usable_total_bytes", int64_t{0});
   		out["runtime_mode"] = runtime_pool.value("runtime_mode", "");
	} else {
   		out["uuid"] = "";
   		out["label"] = "";
   		out["devices"] = 0;
   		out["profile_data"] = "";
	    out["profile_metadata"] = "";
   		out["size_bytes"] = int64_t{0};
   		out["used_bytes"] = int64_t{0};
   		out["resolved_source"] = "";
   		out["resolved_disk"] = "";
   		out["fstype"] = "";
	    out["free_estimated_bytes"] = int64_t{0};
   		out["usable_total_bytes"] = int64_t{0};
	    out["runtime_mode"] = "";
	}

    // ----------------------------
    // Mode
    // Prefer config mode if present and non-empty.
    // Otherwise infer from runtime.
    // ----------------------------
    std::string mode;
    if (has_cfg && cfg_pool.contains("mode") && cfg_pool["mode"].is_string()) {
        mode = trim_copy_safe(cfg_pool["mode"].get<std::string>());
    }
    if (mode.empty()) {
        mode = pool_mode_from_profiles_best_effort(
            out.value("profile_data", ""),
            out.value("profile_metadata", "")
        );
    }
    if (mode != "raid1") mode = "single";
    out["mode"] = mode;

    // ----------------------------
    // Display name
    // Prefer config display_name, then runtime label, then pool_id.
    // ----------------------------
    std::string display_name;
    if (has_cfg && cfg_pool.contains("display_name") && cfg_pool["display_name"].is_string()) {
        display_name = trim_copy_safe(cfg_pool["display_name"].get<std::string>());
    }
    if (display_name.empty()) {
        display_name = trim_copy_safe(out.value("label", ""));
    }
    if (display_name.empty()) {
        display_name = pool_id;
    }
    out["display_name"] = display_name;

    // ----------------------------
    // Slots / membership
    // ----------------------------
    out["member_parent_disks"] = runtime_member_parents;

    std::vector<json> runtime_members;
    runtime_members.reserve(runtime_member_parents.size());

    for (const auto& dev : runtime_member_parents) {
        json m = runtime_identity_for_dev(dev);
        if (!m.is_object()) m = json::object();
        if (!m.contains("runtime_dev") || !m["runtime_dev"].is_string()) {
            m["runtime_dev"] = dev;
        }
        runtime_members.push_back(std::move(m));
    }

    out["runtime_members"] = runtime_members;

    json slots = json::array();

    int assigned_count = 0;
    int matched_count = 0;
    std::set<int> matched_runtime_indexes;

    if (has_cfg && cfg_pool.contains("slots") && cfg_pool["slots"].is_array() && !cfg_pool["slots"].empty()) {
        for (const auto& s : cfg_pool["slots"]) {
            const int index = s.value("index", static_cast<int>(slots.size()));

            std::string dev;
            if (s.contains("device") && s["device"].is_string()) {
                dev = trim_copy_safe(s["device"].get<std::string>());
            }

            const bool assigned = !dev.empty();
            if (assigned) ++assigned_count;

            int match_idx = -1;
            if (assigned) {
                match_idx = find_matching_runtime_member(
                    s,
                    dev,
                    runtime_members,
                    matched_runtime_indexes
                );
            }

            const bool present = match_idx >= 0;
            if (present) {
                matched_runtime_indexes.insert(match_idx);
                ++matched_count;
            }

            std::string runtime_dev;
            if (present) {
                runtime_dev = json_string_trimmed(runtime_members[static_cast<size_t>(match_idx)], "runtime_dev");
            }

            json one = {
                {"index", index},
                {"device", assigned ? json(!runtime_dev.empty() ? runtime_dev : dev) : json(nullptr)},
                {"assigned", assigned},
                {"present", present},
                {"member", present}
            };

            if (s.is_object()) {
                copy_slot_identity_fields(&one, s, false);
            }

            if (present) {
                copy_slot_identity_fields(&one, runtime_members[static_cast<size_t>(match_idx)], true);
            }

            slots.push_back(std::move(one));
        }
    } else {
        // No config slots: best-effort slots from runtime members.
        int idx = 0;
        for (const auto& m : runtime_members) {
            const std::string dev = json_string_trimmed(m, "runtime_dev");

            json one = {
                {"index", idx++},
                {"device", dev.empty() ? json(nullptr) : json(dev)},
                {"assigned", !dev.empty()},
                {"present", true},
                {"member", true}
            };

            copy_slot_identity_fields(&one, m, true);

            if (!dev.empty()) {
                ++assigned_count;
                ++matched_count;
                matched_runtime_indexes.insert(idx - 1);
            }

            slots.push_back(std::move(one));
        }
    }

    out["slots"] = slots;
    out["slot_count"] = has_cfg
        ? std::max(cfg_pool.value("slot_count", static_cast<int>(slots.size())),
                   static_cast<int>(slots.size()))
        : static_cast<int>(slots.size());

    // ----------------------------
    // Status
    // ----------------------------
    const bool mounted = has_rt;

    const bool layout_drift =
        mounted &&
        (
            assigned_count != static_cast<int>(runtime_members.size()) ||
            matched_count != assigned_count ||
            matched_count != static_cast<int>(runtime_members.size())
        );

    const bool degraded =
        mounted &&
        assigned_count > 0 &&
        static_cast<int>(runtime_members.size()) < assigned_count;

    out["status"] = json{
        {"mounted", mounted},
        {"busy", busy},
        {"busy_lock", busy_lock},
        {"degraded", degraded},
        {"layout_drift", layout_drift},
        {"runtime_missing", !mounted}
    };

    return out;
}

} // namespace pqnas