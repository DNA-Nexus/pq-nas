#include "storage_path_validation.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

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

bool write_file(
    const fs::path& path,
    const std::string& text
) {
    std::ofstream output(
        path,
        std::ios::binary
    );

    if (!output.good()) {
        return false;
    }

    output << text;
    return output.good();
}

} // namespace

int main() {
    std::error_code ec;

    const auto unique =
        std::chrono::steady_clock::now()
            .time_since_epoch()
            .count();

    const fs::path base =
        fs::temp_directory_path() /
        (
            "pqnas-storage-containment-" +
            std::to_string(unique)
        );

    const fs::path root =
        base / "root";

    const fs::path sibling =
        base / "root_evil";

    const fs::path outside =
        base / "outside";

    fs::create_directories(
        root / "docs",
        ec
    );

    expect(
        !ec,
        "create user root"
    );

    ec.clear();

    fs::create_directories(
        sibling,
        ec
    );

    expect(
        !ec,
        "create sibling root"
    );

    ec.clear();

    fs::create_directories(
        outside,
        ec
    );

    expect(
        !ec,
        "create outside root"
    );

    const fs::path inside_file =
        root / "docs" / "inside.txt";

    const fs::path sibling_file =
        sibling / "sibling.txt";

    const fs::path outside_file =
        outside / "secret.txt";

    expect(
        write_file(
            inside_file,
            "inside"
        ),
        "write inside fixture"
    );

    expect(
        write_file(
            sibling_file,
            "sibling"
        ),
        "write sibling fixture"
    );

    expect(
        write_file(
            outside_file,
            "secret"
        ),
        "write outside fixture"
    );

    fs::path resolved;
    std::string error;

    expect(
        pqnas::canonical_existing_path_within_root(
            root,
            inside_file,
            &resolved,
            &error
        ),
        "ordinary inside file accepted"
    );

    expect(
        resolved ==
            fs::canonical(
                inside_file
            ),
        "inside file returned canonically"
    );

    resolved =
        "/stale/value";

    expect(
        !pqnas::canonical_existing_path_within_root(
            root,
            sibling_file,
            &resolved,
            &error
        ),
        "sibling string-prefix path rejected"
    );

    expect(
        resolved.empty(),
        "failed sibling lookup clears output"
    );

    ec.clear();

    fs::create_directory_symlink(
        outside,
        root / "escape_dir",
        ec
    );

    expect(
        !ec,
        "create intermediate symlink fixture"
    );

    if (!ec) {
        resolved =
            "/stale/value";

        expect(
            !pqnas::canonical_existing_path_within_root(
                root,
                root /
                    "escape_dir" /
                    "secret.txt",
                &resolved,
                &error
            ),
            "intermediate symlink escape rejected"
        );

        expect(
            resolved.empty(),
            "symlink escape clears output"
        );
    }

    ec.clear();

    fs::create_symlink(
        outside_file,
        root / "escape_file",
        ec
    );

    expect(
        !ec,
        "create final symlink fixture"
    );

    if (!ec) {
        expect(
            !pqnas::canonical_existing_path_within_root(
                root,
                root / "escape_file",
                &resolved,
                &error
            ),
            "final symlink escape rejected"
        );
    }

    expect(
        !pqnas::canonical_existing_path_within_root(
            "relative/root",
            inside_file,
            &resolved,
            &error
        ),
        "relative root rejected"
    );

    expect(
        !pqnas::canonical_existing_path_within_root(
            root,
            root / "missing.txt",
            &resolved,
            &error
        ),
        "missing candidate rejected"
    );

    fs::remove_all(
        base,
        ec
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " canonical containment test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: canonical storage path containment tests passed\n";

    return 0;
}
