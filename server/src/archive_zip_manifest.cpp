#include "archive_zip_manifest.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace pqnas {
namespace {

std::uint16_t le16(const unsigned char* p) {
    return static_cast<std::uint16_t>(p[0]) |
           (static_cast<std::uint16_t>(p[1]) << 8);
}

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

bool has_drive_or_abs_prefix(const std::string& s) {
    if (s.empty()) return false;
    if (s[0] == '/' || s[0] == '\\') return true;
    if (s.size() >= 2 && std::isalpha(static_cast<unsigned char>(s[0])) && s[1] == ':') return true;
    return false;
}


std::string normalize_zip_path(std::string s);
bool is_dangerous_zip_path(const std::string& raw);

std::uint32_t crc32_update_local(std::uint32_t crc, const unsigned char* data, std::size_t len) {
    static std::uint32_t table[256]{};
    static bool init = false;

    if (!init) {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int j = 0; j < 8; ++j) {
                c = (c & 1U) ? (0xEDB88320U ^ (c >> 1U)) : (c >> 1U);
            }
            table[i] = c;
        }
        init = true;
    }

    crc = ~crc;
    for (std::size_t i = 0; i < len; ++i) {
        crc = table[(crc ^ data[i]) & 0xffU] ^ (crc >> 8U);
    }
    return ~crc;
}

bool tar_block_is_zero_local(const std::array<unsigned char, 512>& block) {
    for (unsigned char c : block) {
        if (c != 0) return false;
    }
    return true;
}

std::uint64_t parse_tar_octal_local(const char* p, std::size_t n, bool* ok) {
    if (ok) *ok = false;
    if (!p || n == 0) return 0;

    // POSIX octal field. Ignore base-256 GNU extension for this MVP.
    if (static_cast<unsigned char>(p[0]) & 0x80U) return 0;

    std::uint64_t v = 0;
    std::size_t i = 0;

    while (i < n && (p[i] == ' ' || p[i] == '\0')) ++i;

    bool any = false;
    for (; i < n; ++i) {
        const char c = p[i];
        if (c == '\0' || c == ' ') break;
        if (c < '0' || c > '7') return 0;

        any = true;
        v = (v << 3U) + static_cast<std::uint64_t>(c - '0');
    }

    if (ok) *ok = any;
    return any ? v : 0;
}

std::string tar_field_string_local(const unsigned char* p, std::size_t n) {
    std::size_t len = 0;
    while (len < n && p[len] != '\0') ++len;
    return std::string(reinterpret_cast<const char*>(p), len);
}

std::string trim_tar_payload_string_local(std::string s) {
    while (!s.empty() && (s.back() == '\0' || s.back() == '\n' || s.back() == '\r')) {
        s.pop_back();
    }
    return s;
}

std::string tar_join_prefix_name_local(const std::string& prefix, const std::string& name) {
    if (prefix.empty()) return name;
    if (name.empty()) return prefix;
    return prefix + "/" + name;
}

bool read_exact_local(std::ifstream& f, char* dst, std::size_t n) {
    if (n == 0) return true;
    f.read(dst, static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(f.gcount()) == n;
}

bool skip_bytes_local(std::ifstream& f, std::uint64_t n) {
    if (n == 0) return true;

    f.seekg(static_cast<std::streamoff>(n), std::ios::cur);
    if (f.good()) return true;

    // Fallback for streams where seek fails.
    f.clear();
    std::array<char, 64 * 1024> buf{};
    std::uint64_t left = n;

    while (left > 0) {
        const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
        if (!read_exact_local(f, buf.data(), chunk)) return false;
        left -= chunk;
    }

    return true;
}

bool read_tar_payload_crc_local(std::ifstream& f,
                                std::uint64_t size,
                                std::uint32_t* out_crc) {
    std::array<unsigned char, 64 * 1024> buf{};
    std::uint64_t left = size;
    std::uint32_t crc = 0;

    while (left > 0) {
        const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(left, buf.size()));
        f.read(reinterpret_cast<char*>(buf.data()), static_cast<std::streamsize>(chunk));
        if (static_cast<std::size_t>(f.gcount()) != chunk) return false;

        crc = crc32_update_local(crc, buf.data(), chunk);
        left -= chunk;
    }

    if (out_crc) *out_crc = crc;
    return true;
}

std::string read_tar_payload_string_local(std::ifstream& f, std::uint64_t size) {
    if (size > 1024 * 1024) return {};

    std::string out;
    out.resize(static_cast<std::size_t>(size));
    if (size > 0 && !read_exact_local(f, out.data(), out.size())) return {};
    return trim_tar_payload_string_local(out);
}

std::string pax_path_from_payload_local(const std::string& payload) {
    std::size_t pos = 0;

    while (pos < payload.size()) {
        std::size_t sp = payload.find(' ', pos);
        if (sp == std::string::npos) break;

        std::uint64_t len = 0;
        for (std::size_t i = pos; i < sp; ++i) {
            if (payload[i] < '0' || payload[i] > '9') {
                len = 0;
                break;
            }
            len = len * 10 + static_cast<std::uint64_t>(payload[i] - '0');
        }

        if (len == 0 || pos + len > payload.size()) break;

        const std::size_t kv_start = sp + 1;
        const std::size_t kv_end = pos + static_cast<std::size_t>(len);
        std::string rec = payload.substr(kv_start, kv_end - kv_start);

        if (!rec.empty() && rec.back() == '\n') rec.pop_back();

        const std::string key = "path=";
        if (rec.rfind(key, 0) == 0) {
            return rec.substr(key.size());
        }

        pos += static_cast<std::size_t>(len);
    }

    return {};
}

ZipManifestResult read_tar_manifest_from_file_local(
    const std::filesystem::path& tar_path,
    std::size_t max_entries
) {
    ZipManifestResult out;

    std::ifstream f(tar_path, std::ios::binary);
    if (!f) {
        out.error = "failed to open tar";
        return out;
    }

    std::string pending_long_name;
    std::string pending_pax_path;
    std::size_t count = 0;

    while (true) {
        std::array<unsigned char, 512> header{};
        f.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
        const auto got = f.gcount();

        if (got == 0) break;
        if (got != static_cast<std::streamsize>(header.size())) {
            out.error = "truncated tar header";
            return out;
        }

        if (tar_block_is_zero_local(header)) break;

        const char typeflag = static_cast<char>(header[156]);

        bool size_ok = false;
        const std::uint64_t size = parse_tar_octal_local(
            reinterpret_cast<const char*>(header.data() + 124),
            12,
            &size_ok
        );

        if (!size_ok) {
            out.error = "invalid tar size field";
            return out;
        }

        const std::uint64_t padded = ((size + 511ULL) / 512ULL) * 512ULL;
        const std::uint64_t padding = padded - size;

        std::string name = tar_field_string_local(header.data(), 100);
        std::string prefix = tar_field_string_local(header.data() + 345, 155);
        std::string path = tar_join_prefix_name_local(prefix, name);

        if (typeflag == 'L') {
            pending_long_name = read_tar_payload_string_local(f, size);
            if (!skip_bytes_local(f, padding)) {
                out.error = "failed to skip tar longname padding";
                return out;
            }
            continue;
        }

        if (typeflag == 'x') {
            std::string pax = read_tar_payload_string_local(f, size);
            pending_pax_path = pax_path_from_payload_local(pax);
            if (!skip_bytes_local(f, padding)) {
                out.error = "failed to skip tar pax padding";
                return out;
            }
            continue;
        }

        if (!pending_long_name.empty()) {
            path = pending_long_name;
            pending_long_name.clear();
        }

        if (!pending_pax_path.empty()) {
            path = pending_pax_path;
            pending_pax_path.clear();
        }

        path = normalize_zip_path(path);

        const bool is_regular =
            typeflag == '\0' ||
            typeflag == '0';

        if (is_regular && !path.empty() && path.back() != '/' && !is_dangerous_zip_path(path)) {
            if (++count > max_entries) {
                out.error = "tar has too many entries";
                return out;
            }

            std::uint32_t crc = 0;
            if (!read_tar_payload_crc_local(f, size, &crc)) {
                out.error = "failed to read tar file payload";
                return out;
            }

            if (!skip_bytes_local(f, padding)) {
                out.error = "failed to skip tar file padding";
                return out;
            }

            ZipManifestEntry ent;
            ent.path = std::move(path);
            ent.size = size;
            ent.compressed_size = size;
            ent.crc32 = crc;
            out.entries.push_back(std::move(ent));
            continue;
        }

        if (!skip_bytes_local(f, padded)) {
            out.error = "failed to skip tar payload";
            return out;
        }
    }

    std::sort(out.entries.begin(), out.entries.end(), [](const ZipManifestEntry& a, const ZipManifestEntry& b) {
        return a.path < b.path;
    });

    out.ok = true;
    return out;
}

bool path_has_ext_lower_local(const std::filesystem::path& p, const std::string& ext) {
    std::string e = p.extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return e == ext;
}

bool is_dangerous_zip_path(const std::string& raw) {
    if (has_drive_or_abs_prefix(raw)) return true;

    std::string s = raw;
    std::replace(s.begin(), s.end(), '\\', '/');

    std::size_t pos = 0;
    while (pos <= s.size()) {
        std::size_t next = s.find('/', pos);
        std::string part = s.substr(pos, next == std::string::npos ? std::string::npos : next - pos);

        if (part == "..") return true;

        if (next == std::string::npos) break;
        pos = next + 1;
    }

    return false;
}

std::string normalize_zip_path(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');

    while (!s.empty() && s.front() == '/') {
        s.erase(s.begin());
    }

    return s;
}

} // namespace

std::string zip_crc32_hex(std::uint32_t crc) {
    std::ostringstream oss;
    oss << std::hex << std::nouppercase << std::setfill('0') << std::setw(8) << crc;
    return oss.str();
}

ZipManifestResult read_zip_manifest_from_file(
    const std::filesystem::path& zip_path,
    std::size_t max_entries
) {
    ZipManifestResult out;

    std::ifstream f(zip_path, std::ios::binary);
    if (!f) {
        out.error = "failed to open zip";
        return out;
    }

    f.seekg(0, std::ios::end);
    const std::streamoff size_off = f.tellg();
    if (size_off < 0) {
        out.error = "failed to stat zip";
        return out;
    }

    const std::uint64_t file_size = static_cast<std::uint64_t>(size_off);
    if (file_size < 22) {
        out.error = "not a zip file";
        return out;
    }

    const std::uint64_t tail_size_u64 = std::min<std::uint64_t>(file_size, 22 + 65535);
    const std::size_t tail_size = static_cast<std::size_t>(tail_size_u64);
    std::vector<unsigned char> tail(tail_size);

    f.seekg(static_cast<std::streamoff>(file_size - tail_size_u64), std::ios::beg);
    f.read(reinterpret_cast<char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
    if (static_cast<std::size_t>(f.gcount()) != tail.size()) {
        out.error = "failed to read zip tail";
        return out;
    }

    const std::array<unsigned char, 4> eocd_sig{{0x50, 0x4b, 0x05, 0x06}};
    std::size_t eocd_pos = static_cast<std::size_t>(-1);

    if (tail.size() >= 22) {
        for (std::size_t i = tail.size() - 22;; --i) {
            if (tail[i] == eocd_sig[0] &&
                tail[i + 1] == eocd_sig[1] &&
                tail[i + 2] == eocd_sig[2] &&
                tail[i + 3] == eocd_sig[3]) {
                eocd_pos = i;
                break;
            }

            if (i == 0) break;
        }
    }

    if (eocd_pos == static_cast<std::size_t>(-1)) {
        out.error = "zip central directory not found";
        return out;
    }

    const unsigned char* e = tail.data() + eocd_pos;

    const std::uint16_t disk_no = le16(e + 4);
    const std::uint16_t cd_disk = le16(e + 6);
    const std::uint16_t entries_this_disk = le16(e + 8);
    const std::uint16_t total_entries16 = le16(e + 10);
    const std::uint32_t cd_size32 = le32(e + 12);
    const std::uint32_t cd_offset32 = le32(e + 16);

    if (disk_no != 0 || cd_disk != 0 || entries_this_disk != total_entries16) {
        out.error = "multi-disk zip is not supported";
        return out;
    }

    if (total_entries16 == 0xffff || cd_size32 == 0xffffffffu || cd_offset32 == 0xffffffffu) {
        out.zip64 = true;
        out.error = "zip64 is not supported yet";
        return out;
    }

    const std::uint64_t total_entries = total_entries16;
    const std::uint64_t cd_size = cd_size32;
    const std::uint64_t cd_offset = cd_offset32;

    if (total_entries > max_entries) {
        out.error = "zip has too many entries";
        return out;
    }

    if (cd_offset > file_size || cd_size > file_size || cd_offset + cd_size > file_size) {
        out.error = "invalid zip central directory";
        return out;
    }

    std::vector<unsigned char> cd(static_cast<std::size_t>(cd_size));
    f.clear();
    f.seekg(static_cast<std::streamoff>(cd_offset), std::ios::beg);
    f.read(reinterpret_cast<char*>(cd.data()), static_cast<std::streamsize>(cd.size()));
    if (static_cast<std::size_t>(f.gcount()) != cd.size()) {
        out.error = "failed to read zip central directory";
        return out;
    }

    std::size_t pos = 0;
    out.entries.reserve(static_cast<std::size_t>(total_entries));

    for (std::uint64_t i = 0; i < total_entries; ++i) {
        if (pos + 46 > cd.size()) {
            out.error = "truncated zip central directory";
            out.entries.clear();
            return out;
        }

        const unsigned char* h = cd.data() + pos;
        if (!(h[0] == 0x50 && h[1] == 0x4b && h[2] == 0x01 && h[3] == 0x02)) {
            out.error = "invalid zip central directory entry";
            out.entries.clear();
            return out;
        }

        const std::uint32_t crc = le32(h + 16);
        const std::uint32_t compressed_size = le32(h + 20);
        const std::uint32_t uncompressed_size = le32(h + 24);
        const std::uint16_t name_len = le16(h + 28);
        const std::uint16_t extra_len = le16(h + 30);
        const std::uint16_t comment_len = le16(h + 32);

        const std::size_t name_pos = pos + 46;
        const std::size_t name_end = name_pos + name_len;
        const std::size_t next_pos = name_end + extra_len + comment_len;

        if (name_end > cd.size() || next_pos > cd.size()) {
            out.error = "invalid zip central directory name length";
            out.entries.clear();
            return out;
        }

        std::string raw_name(reinterpret_cast<const char*>(cd.data() + name_pos), name_len);
        std::string path = normalize_zip_path(raw_name);

        if (!path.empty() && path.back() != '/' && !is_dangerous_zip_path(path)) {
            ZipManifestEntry ent;
            ent.path = std::move(path);
            ent.size = uncompressed_size;
            ent.compressed_size = compressed_size;
            ent.crc32 = crc;
            out.entries.push_back(std::move(ent));
        }

        pos = next_pos;
    }

    std::sort(out.entries.begin(), out.entries.end(), [](const ZipManifestEntry& a, const ZipManifestEntry& b) {
        return a.path < b.path;
    });

    out.ok = true;
    return out;
}

ZipManifestResult read_archive_manifest_from_file(
    const std::filesystem::path& archive_path,
    std::size_t max_entries
) {
    return read_archive_manifest_from_file(archive_path, archive_path.filename().string(), max_entries);
}

ZipManifestResult read_archive_manifest_from_file(
    const std::filesystem::path& archive_path,
    const std::string& logical_name,
    std::size_t max_entries
) {
    const std::filesystem::path logical_path(logical_name);

    if (path_has_ext_lower_local(logical_path, ".tar") || path_has_ext_lower_local(archive_path, ".tar")) {
        return read_tar_manifest_from_file_local(archive_path, max_entries);
    }

    return read_zip_manifest_from_file(archive_path, max_entries);
}


} // namespace pqnas
