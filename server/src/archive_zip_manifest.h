#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pqnas {

struct ZipManifestEntry {
    std::string path;
    std::uint64_t size = 0;
    std::uint64_t compressed_size = 0;
    std::uint32_t crc32 = 0;
};

struct ZipManifestResult {
    bool ok = false;
    bool zip64 = false;
    std::string error;
    std::vector<ZipManifestEntry> entries;
};

ZipManifestResult read_zip_manifest_from_file(
    const std::filesystem::path& zip_path,
    std::size_t max_entries = 20000
);

std::string zip_crc32_hex(std::uint32_t crc);

} // namespace pqnas
