#include "session_cookie.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <sodium.h>
#include <string>

namespace {

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr << "FAIL: sodium_init\n";
        return 1;
    }

    std::array<unsigned char, 32> base_key{};

    for (std::size_t i = 0; i < base_key.size(); ++i) {
        base_key[i] = static_cast<unsigned char>(i + 1);
    }

    const std::string user_claim_b64 =
        "YWFhYWFhYWFhYWFhYWFhYQ==";

    const std::string workspace_claim_b64 =
        "ZXh0ZXJuYWx8d29ya3NwYWNlfGZwfHZpZXdlcg==";

    std::string user_cookie;
    std::string workspace_cookie;

    expect(
        session_cookie_mint(
            base_key.data(),
            user_claim_b64,
            1000,
            1600,
            user_cookie
        ),
        "normal user session cookie mints"
    );

    expect(
        workspace_session_cookie_mint(
            base_key.data(),
            workspace_claim_b64,
            1000,
            1600,
            workspace_cookie
        ),
        "workspace session cookie mints"
    );

    expect(
        !user_cookie.empty() &&
        !workspace_cookie.empty() &&
        user_cookie != workspace_cookie,
        "cookie families produce distinct MAC-bearing values"
    );

    std::string claim;
    long exp = 0;

    expect(
        session_cookie_verify(
            base_key.data(),
            user_cookie,
            claim,
            exp
        ) &&
        claim == user_claim_b64 &&
        exp == 1600,
        "normal user session verifies with raw user-session key"
    );

    claim = "stale";
    exp = 9999;

    expect(
        workspace_session_cookie_verify(
            base_key.data(),
            workspace_cookie,
            claim,
            exp
        ) &&
        claim == workspace_claim_b64 &&
        exp == 1600,
        "workspace session verifies with derived workspace key"
    );

    claim = "stale";
    exp = 9999;

    expect(
        !workspace_session_cookie_verify(
            base_key.data(),
            user_cookie,
            claim,
            exp
        ),
        "pqnas_session fails workspace-family MAC verification"
    );

    expect(
        claim.empty() && exp == 0,
        "workspace verifier clears outputs after cross-family rejection"
    );

    claim = "stale";
    exp = 9999;

    expect(
        !session_cookie_verify(
            base_key.data(),
            workspace_cookie,
            claim,
            exp
        ),
        "pqnas_workspace_session fails normal-session MAC verification"
    );

    std::array<unsigned char, 32> zero_key{};
    std::string output = "stale";

    expect(
        !workspace_session_cookie_mint(
            zero_key.data(),
            workspace_claim_b64,
            1000,
            1600,
            output
        ) &&
        output.empty(),
        "zero base key cannot mint workspace session"
    );

    if (failures != 0) {
        std::cerr
            << "FAILED: "
            << failures
            << " workspace session domain tests\n";
        return 1;
    }

    std::cout
        << "OK: workspace/user session cookie domains are cryptographically separated\n";
    return 0;
}
