#include "cluster/node_enrollment_token.h"

#include <sodium.h>

#include <iostream>
#include <set>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

using pqnas::cluster::
    NodeEnrollmentTokenMaterial;

using pqnas::cluster::
    NodeEnrollmentTokenResult;

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

void expect_result(
    NodeEnrollmentTokenResult actual,
    NodeEnrollmentTokenResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                node_enrollment_token_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                node_enrollment_token_result_code(
                    actual
                )
    );
}

bool base64url_text(
    const std::string& value
) {
    for (unsigned char ch : value) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_') {
            return false;
        }
    }

    return true;
}

} // namespace

static_assert(
    !std::is_copy_constructible_v<
        NodeEnrollmentTokenMaterial
    >
);

static_assert(
    !std::is_copy_assignable_v<
        NodeEnrollmentTokenMaterial
    >
);

static_assert(
    std::is_move_constructible_v<
        NodeEnrollmentTokenMaterial
    >
);

static_assert(
    std::is_move_assignable_v<
        NodeEnrollmentTokenMaterial
    >
);

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";
        return 1;
    }

    const std::string known_token =
        "AAECAwQFBgcICQoLDA0ODxAREhMUFRYX"
        "GBkaGxwdHh8";

    const std::string known_hash =
        "Yw3NKWbEM2aRElRIu7JbT_QSpJxzLbLI"
        "q8G4WBvXEN0";

    std::string computed_hash = "stale";

    expect_result(
        pqnas::cluster::
            hash_node_enrollment_token(
                known_token,
                &computed_hash
            ),
        NodeEnrollmentTokenResult::Ok,
        "deterministic SHA-256 vector"
    );

    expect(
        computed_hash == known_hash,
        "deterministic hash output"
    );

    expect(
        pqnas::cluster::
            valid_node_enrollment_token_hash(
                known_hash
            ),
        "known canonical hash"
    );

    const std::vector<std::string>
        invalid_tokens{
            "",
            std::string(42, 'A'),
            std::string(44, 'A'),
            std::string(43, 'B'),
            std::string(42, 'A') + "=",
            std::string(42, 'A') + "!"
        };

    for (const std::string& invalid :
         invalid_tokens) {
        computed_hash = "stale";

        expect_result(
            pqnas::cluster::
                hash_node_enrollment_token(
                    invalid,
                    &computed_hash
                ),
            NodeEnrollmentTokenResult::
                InvalidInput,
            "invalid plaintext token"
        );

        expect(
            computed_hash.empty(),
            "invalid token must clear hash output"
        );
    }

    expect_result(
        pqnas::cluster::
            hash_node_enrollment_token(
                known_token,
                nullptr
            ),
        NodeEnrollmentTokenResult::
            InvalidInput,
        "missing hash output"
    );

    expect(
        !pqnas::cluster::
            valid_node_enrollment_token_hash(
                std::string(43, 'B')
            ),
        "non-canonical hash must fail"
    );

    expect_result(
        pqnas::cluster::
            generate_node_enrollment_token(
                nullptr
            ),
        NodeEnrollmentTokenResult::
            InvalidInput,
        "missing generation output"
    );

    std::set<std::string> generated_tokens;

    for (int iteration = 0;
         iteration < 32;
         ++iteration) {
        NodeEnrollmentTokenMaterial material;

        expect_result(
            pqnas::cluster::
                generate_node_enrollment_token(
                    &material
                ),
            NodeEnrollmentTokenResult::Ok,
            "generate enrollment token"
        );

        expect(
            material.configured(),
            "generated material should be configured"
        );

        const std::string stored_hash =
            material.token_hash();

        expect(
            stored_hash.size() ==
                pqnas::cluster::
                    kNodeEnrollmentTokenHashChars &&
            base64url_text(stored_hash) &&
            pqnas::cluster::
                valid_node_enrollment_token_hash(
                    stored_hash
                ),
            "generated verifier format"
        );

        std::string plaintext = "old-secret";

        expect(
            material.take_token(
                &plaintext
            ),
            "take generated plaintext once"
        );

        expect(
            !material.configured() &&
            material.token_hash().empty(),
            "take should clear material state"
        );

        expect(
            plaintext.size() ==
                pqnas::cluster::
                    kNodeEnrollmentTokenChars &&
            base64url_text(plaintext),
            "generated plaintext format"
        );

        expect(
            generated_tokens.insert(
                plaintext
            ).second,
            "generated token collision"
        );

        std::string recomputed;

        expect_result(
            pqnas::cluster::
                hash_node_enrollment_token(
                    plaintext,
                    &recomputed
                ),
            NodeEnrollmentTokenResult::Ok,
            "rehash generated plaintext"
        );

        expect(
            recomputed == stored_hash,
            "generated token and verifier must match"
        );

        expect(
            !material.take_token(
                &recomputed
            ),
            "plaintext must not be transferable twice"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &plaintext
            );

        expect(
            plaintext.empty(),
            "plaintext clearing helper"
        );
    }

    {
        NodeEnrollmentTokenMaterial original;

        expect_result(
            pqnas::cluster::
                generate_node_enrollment_token(
                    &original
                ),
            NodeEnrollmentTokenResult::Ok,
            "generate move fixture"
        );

        const std::string expected_hash =
            original.token_hash();

        NodeEnrollmentTokenMaterial moved(
            std::move(original)
        );

        expect(
            !original.configured() &&
            moved.configured() &&
            moved.token_hash() ==
                expected_hash,
            "move construction transfers ownership"
        );

        std::string plaintext;

        expect(
            moved.take_token(
                &plaintext
            ),
            "moved material transfers plaintext"
        );

        std::string moved_hash;

        expect_result(
            pqnas::cluster::
                hash_node_enrollment_token(
                    plaintext,
                    &moved_hash
                ),
            NodeEnrollmentTokenResult::Ok,
            "hash moved plaintext"
        );

        expect(
            moved_hash == expected_hash,
            "moved material verifier"
        );

        pqnas::cluster::
            clear_node_enrollment_token_text(
                &plaintext
            );
    }

    expect(
        std::string(
            pqnas::cluster::
                node_enrollment_token_result_code(
                    NodeEnrollmentTokenResult::
                        CryptoUnavailable
                )
        ) ==
            "enrollment_token_crypto_unavailable",
        "stable crypto-unavailable reason code"
    );

    if (failures != 0) {
        std::cerr
            << failures
            << " node enrollment token test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: node enrollment token tests passed\n";

    return 0;
}
