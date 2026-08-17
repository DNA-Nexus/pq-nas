#include "storage_path_validation.h"

#include <iostream>
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
            << '\n';
    }
}

void expect_normalizes_to(
    const std::string& input,
    const std::string& expected,
    const std::string& message
) {
    std::string normalized;
    std::string error;

    const bool ok =
        pqnas::normalize_storage_relative_path_strict(
            input,
            &normalized,
            &error
        );

    expect(
        ok && normalized == expected,
        message
    );
}

void expect_rejected(
    const std::string& input,
    const std::string& message
) {
    std::string normalized = "must-not-survive";
    std::string error;

    const bool ok =
        pqnas::normalize_storage_relative_path_strict(
            input,
            &normalized,
            &error
        );

    expect(
        !ok,
        message
    );
}

} // namespace

int main() {
    expect_normalizes_to(
        "users/abc",
        "users/abc",
        "canonical relative path accepted unchanged"
    );

    expect_normalizes_to(
        "movies//video.mkv",
        "movies/video.mkv",
        "redundant separators are normalized"
    );

    // Preserve the existing File API behavior during this refactor:
    // lexical normalization happens before component validation.
    expect_normalizes_to(
        "movies/../video.mkv",
        "video.mkv",
        "existing lexical normalization semantics preserved"
    );

    expect_rejected(
        "",
        "empty path rejected"
    );

    expect_rejected(
        "/etc/passwd",
        "absolute path rejected"
    );

    expect_rejected(
        "../outside",
        "escaping traversal rejected"
    );

    expect_rejected(
        ".pqnas",
        "reserved root namespace rejected"
    );

    expect_rejected(
        "users/.pqnas/state",
        "reserved nested namespace rejected"
    );

    const std::string with_nul(
        "users/\0escape",
        13
    );

    expect_rejected(
        with_nul,
        "embedded NUL rejected"
    );

    {
        std::string error;

        const bool ok =
            pqnas::normalize_storage_relative_path_strict(
                "users/abc",
                nullptr,
                &error
            );

        expect(
            !ok,
            "null normalization output rejected"
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " storage path validation test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage path validation tests passed\n";

    return 0;
}
