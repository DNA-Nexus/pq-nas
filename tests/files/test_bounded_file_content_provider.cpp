#include "bounded_file_content_provider.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <limits>
#include <string>

namespace {

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;
        std::cerr
            << "FAIL: "
            << message
            << "\n";
    }
}

} // namespace

int main() {
    namespace fs = std::filesystem;

    const fs::path path =
        fs::temp_directory_path() /
        "pqnas_rt012_sparse_version_blob.bin";

    std::error_code ec;
    fs::remove(path, ec);
    ec.clear();

    constexpr std::uint64_t sparse_size =
        (2ULL * 1024ULL * 1024ULL * 1024ULL) +
        (8ULL * 1024ULL * 1024ULL) +
        4096ULL;

    constexpr std::uint64_t marker_offset =
        (2ULL * 1024ULL * 1024ULL * 1024ULL) +
        12345ULL;

    const std::string marker =
        "RT012-HIGH-OFFSET";

    {
        std::ofstream output(
            path,
            std::ios::binary |
            std::ios::trunc
        );

        expect(
            output.good(),
            "sparse fixture opens"
        );

        if (output.good()) {
            output.seekp(
                static_cast<std::streamoff>(
                    marker_offset
                ),
                std::ios::beg
            );
            output.write(
                marker.data(),
                static_cast<std::streamsize>(
                    marker.size()
                )
            );
        }
    }

    fs::resize_file(
        path,
        sparse_size,
        ec
    );

    expect(
        !ec,
        "sparse multi-gigabyte fixture resizes"
    );

    auto input =
        std::make_shared<std::ifstream>(
            path,
            std::ios::binary
        );

    httplib::Response response;

    const bool installed =
        pqnas::install_bounded_file_content_provider(
            response,
            input,
            sparse_size,
            "application/octet-stream"
        );

    expect(
        installed,
        "bounded provider installs for multi-gigabyte blob"
    );

    expect(
        response.body.empty(),
        "provider installation does not materialize response body"
    );

    expect(
        static_cast<bool>(
            response.content_provider_
        ),
        "httplib response has content provider"
    );

    if (sparse_size <=
        static_cast<std::uint64_t>(
            std::numeric_limits<std::size_t>::max()
        )) {
        expect(
            response.content_length_ ==
                static_cast<std::size_t>(
                    sparse_size
                ),
            "provider advertises complete sparse blob length"
        );
    }

    if (response.content_provider_) {
        httplib::DataSink marker_sink;
        std::string marker_output;
        std::size_t max_marker_write = 0;

        marker_sink.write =
            [&](const char* data, std::size_t len) {
                max_marker_write =
                    std::max(
                        max_marker_write,
                        len
                    );
                marker_output.append(
                    data,
                    len
                );
                return true;
            };

        expect(
            response.content_provider_(
                static_cast<std::size_t>(
                    marker_offset
                ),
                marker.size(),
                marker_sink
            ),
            "provider reads high sparse offset"
        );

        expect(
            marker_output == marker,
            "provider returns exact high-offset bytes"
        );

        expect(
            max_marker_write <=
                pqnas::
                    kBoundedFileContentProviderBufferBytes,
            "high-offset read respects provider buffer ceiling"
        );

        httplib::DataSink chunk_sink;
        std::size_t total_written = 0;
        std::size_t max_write = 0;

        chunk_sink.write =
            [&](const char*, std::size_t len) {
                total_written += len;
                max_write =
                    std::max(
                        max_write,
                        len
                    );
                return true;
            };

        constexpr std::size_t requested =
            (64 * 1024) +
            (32 * 1024) +
            17;

        expect(
            response.content_provider_(
                0,
                requested,
                chunk_sink
            ),
            "provider streams multi-buffer request"
        );

        expect(
            total_written == requested,
            "provider streams requested byte count"
        );

        expect(
            max_write <=
                pqnas::
                    kBoundedFileContentProviderBufferBytes,
            "provider never emits a chunk above 64 KiB"
        );
    }

    input.reset();
    fs::remove(path, ec);

    if (failures != 0) {
        std::cerr
            << "FAILED: "
            << failures
            << " bounded file provider tests\n";
        return 1;
    }

    std::cout
        << "OK: multi-gigabyte version blobs use bounded content provider\n";
    return 0;
}
