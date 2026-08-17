#include "storage_metadata_validation.h"

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
        std::cerr << "FAIL: " << message << '\n';
    }
}

} // namespace

int main() {
    expect(
        pqnas::valid_managed_storage_pool_id("pool_01"),
        "normal managed pool ID accepted"
    );

    expect(
        pqnas::valid_managed_storage_pool_id("ab"),
        "minimum-length managed pool ID accepted"
    );

    expect(
        !pqnas::valid_managed_storage_pool_id(""),
        "empty managed pool ID rejected"
    );

    expect(
        !pqnas::valid_managed_storage_pool_id("a"),
        "one-character managed pool ID rejected"
    );

    expect(
        !pqnas::valid_managed_storage_pool_id(
            std::string(25, 'a')
        ),
        "overlong managed pool ID rejected"
    );

    expect(
        !pqnas::valid_managed_storage_pool_id("Pool_01"),
        "uppercase managed pool ID rejected"
    );

    expect(
        !pqnas::valid_managed_storage_pool_id("pool-01"),
        "hyphenated managed pool ID rejected"
    );

    expect(
        !pqnas::valid_managed_storage_pool_id("../pool"),
        "path-like managed pool ID rejected"
    );

    expect(
        pqnas::valid_storage_pool_reference(""),
        "empty persisted pool reference means default"
    );

    expect(
        pqnas::valid_storage_pool_reference("pool_01"),
        "managed persisted pool reference accepted"
    );

    expect(
        !pqnas::valid_storage_pool_reference("../pool"),
        "unsafe persisted pool reference rejected"
    );

    const std::string fp(128, 'a');

    expect(
        pqnas::valid_storage_user_fingerprint_hex(fp),
        "128-char lowercase fingerprint accepted"
    );

    expect(
        !pqnas::valid_storage_user_fingerprint_hex(
            std::string(127, 'a')
        ),
        "short fingerprint rejected"
    );

    expect(
        !pqnas::valid_storage_user_fingerprint_hex(
            std::string(129, 'a')
        ),
        "long fingerprint rejected"
    );

    std::string uppercase_fp = fp;
    uppercase_fp[17] = 'A';

    expect(
        !pqnas::valid_storage_user_fingerprint_hex(
            uppercase_fp
        ),
        "uppercase fingerprint rejected"
    );

    std::string invalid_fp = fp;
    invalid_fp[63] = 'g';

    expect(
        !pqnas::valid_storage_user_fingerprint_hex(
            invalid_fp
        ),
        "non-hex fingerprint rejected"
    );

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " storage metadata validation test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage metadata validation tests passed\n";

    return 0;
}
