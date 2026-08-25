#pragma once

#include <httplib.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <string>

namespace pqnas {

inline constexpr std::size_t
    kBoundedFileContentProviderBufferBytes =
        64 * 1024;

inline bool install_bounded_file_content_provider(
    httplib::Response& response,
    const std::shared_ptr<std::ifstream>& input,
    std::uint64_t content_length,
    const std::string& content_type
) {
    if (!input ||
        !input->is_open() ||
        content_length >
            static_cast<std::uint64_t>(
                std::numeric_limits<std::size_t>::max()
            )) {
        return false;
    }

    response.set_content_provider(
        static_cast<std::size_t>(content_length),
        content_type,
        [input](
            std::size_t offset,
            std::size_t length,
            httplib::DataSink& sink
        ) mutable -> bool {
            if (!input ||
                !input->is_open() ||
                offset >
                    static_cast<std::size_t>(
                        std::numeric_limits<
                            std::streamoff
                        >::max()
                    )) {
                return false;
            }

            input->clear();
            input->seekg(
                static_cast<std::streamoff>(offset),
                std::ios::beg
            );

            if (!input->good()) {
                return false;
            }

            // Security: keep per-request blob memory bounded independently of
            // the stored version size or the HTTP range requested by a client.
            std::array<
                char,
                kBoundedFileContentProviderBufferBytes
            > buffer{};

            std::size_t remaining = length;

            while (remaining > 0) {
                const std::size_t want =
                    std::min(
                        remaining,
                        buffer.size()
                    );

                input->read(
                    buffer.data(),
                    static_cast<std::streamsize>(want)
                );

                const std::streamsize got =
                    input->gcount();

                if (got <= 0) {
                    return false;
                }

                const auto got_size =
                    static_cast<std::size_t>(got);

                if (!sink.write(
                        buffer.data(),
                        got_size
                    )) {
                    return false;
                }

                remaining -= got_size;
            }

            return true;
        }
    );

    return true;
}

} // namespace pqnas
