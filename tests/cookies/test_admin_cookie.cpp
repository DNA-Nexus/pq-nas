// tools/test_admin_cookie.cpp
//
// Regression test: cookie + policy + admin guard must stay compatible.
//
// What it tests:
// 1) Load config/policy.json (admin fingerprint)
// 2) Mint pqnas_session cookie embedding fp_hex (as STANDARD base64 of UTF-8)
// 3) Call require_admin_cookie() with a fake httplib::Request containing that cookie
// 4) Assert admin access is granted (returns true)
//
// Build target should link authz.cc + session_cookie.cc.

#include <sodium.h>
#include <fstream>
#include <sstream>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include "httplib.h"
#include "allowlist.h"
#include "authz.h"
#include "session_cookie.h"
#include "strict_session_cookie.h"

static std::string b64std_enc(const unsigned char* data, size_t len) {
    // Standard base64 with padding (Variant ORIGINAL), matches server decoding.
    const size_t outLen = sodium_base64_encoded_len(len, sodium_base64_VARIANT_ORIGINAL);
    std::string out(outLen, '\0');
    sodium_bin2base64(out.data(), out.size(), data, len, sodium_base64_VARIANT_ORIGINAL);
    out.resize(std::strlen(out.c_str())); // trim trailing '\0'
    return out;
}

static std::string write_temp_policy_file(const std::string& fp_hex) {
    const auto dir = std::filesystem::temp_directory_path() / "pqnas_test_admin_cookie";
    std::filesystem::create_directories(dir);

    const auto path = dir / "policy.json";

    std::ofstream f(path);
    if (!f) return "";

    f << "{\n"
      << "  \"users\": [\n"
      << "    {\n"
      << "      \"fingerprint\": \"" << fp_hex << "\",\n"
      << "      \"role\": \"admin\"\n"
      << "    }\n"
      << "  ]\n"
      << "}\n";

    f.close();
    return path.string();
}

int main() {
    if (sodium_init() < 0) {
        std::cerr << "sodium_init failed\n";
        return 2;
    }

    const std::string fp_hex =
        "11111111111111111111111111111111"
        "22222222222222222222222222222222"
        "33333333333333333333333333333333"
        "44444444444444444444444444444444";

    const std::string policy_path = write_temp_policy_file(fp_hex);
    if (policy_path.empty()) {
        std::cerr << "FAIL: could not create temporary policy fixture\n";
        return 3;
    }

    pqnas::Allowlist allowlist;
    if (!allowlist.load(policy_path)) {
        std::cerr << "FAIL: could not load policy: " << policy_path << "\n";
        return 3;
    }

    // Deterministic cookie key for the test (do NOT randomize, we want stable behavior)
    unsigned char cookie_key[32];
    for (size_t i = 0; i < sizeof(cookie_key); i++) cookie_key[i] = (unsigned char)i;


    // Sanity: policy says this should be admin
    if (!allowlist.is_admin(fp_hex)) {
        std::cerr << "FAIL: policy does not mark fp as admin\n";
        return 4;
    }

    // Mint cookie like the server: cookie stores STANDARD base64 of UTF-8 fingerprint hex string.
    const std::string fp_b64 = b64std_enc(
        reinterpret_cast<const unsigned char*>(fp_hex.data()),
        fp_hex.size()
    );

    // Deterministic "current time" that is safely in the future so the cookie won't expire.
    const long now = 1893456000; // 2030-01-01 00:00:00 UTC
    const long iat = now;
    const long exp = now + 3600; // 1 hour

    std::string cookieVal;
    if (!session_cookie_mint(cookie_key, fp_b64, iat, exp, cookieVal)) {
        std::cerr << "FAIL: session_cookie_mint failed\n";
        return 5;
    }

    auto require_allowed = [&](const std::string& header, const char* label) -> bool {
        httplib::Request request;
        httplib::Response response;
        request.remote_addr = "127.0.0.1";
        request.headers.emplace("Cookie", header);

        if (!require_admin_cookie(request, response, cookie_key, policy_path, &allowlist)) {
            std::cerr << "FAIL: " << label << " status=" << response.status << " body=" << response.body << "\n";
            return false;
        }
        return true;
    };

    auto require_rejected = [&](const httplib::Request& request, const char* label) -> bool {
        httplib::Response response;
        if (require_admin_cookie(request, response, cookie_key, policy_path, &allowlist)) {
            std::cerr << "FAIL: " << label << " unexpectedly authenticated\n";
            return false;
        }
        if (response.status != 401) {
            std::cerr << "FAIL: " << label << " status=" << response.status << " expected=401\n";
            return false;
        }
        return true;
    };

    auto require_workspace_cookie_result =
        [&](
            const httplib::Request& request,
            pqnas::StrictSessionCookieResult expected,
            const std::string& expected_value,
            const char* label
        ) -> bool {
            std::string parsed = "stale";

            const auto result =
                pqnas::extract_strict_session_cookie(
                    request,
                    "pqnas_workspace_session",
                    &parsed
                );

            if (result != expected) {
                std::cerr
                    << "FAIL: "
                    << label
                    << " unexpected parser result\n";
                return false;
            }

            if (expected ==
                    pqnas::StrictSessionCookieResult::Ok &&
                parsed != expected_value) {
                std::cerr
                    << "FAIL: "
                    << label
                    << " parsed wrong cookie value\n";
                return false;
            }

            if (expected !=
                    pqnas::StrictSessionCookieResult::Ok &&
                !parsed.empty()) {
                std::cerr
                    << "FAIL: "
                    << label
                    << " left stale parser output\n";
                return false;
            }

            return true;
        };

    // NEW-003: pqnas_workspace_session must use the same strict credential
    // selection rules as pqnas_session.
    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string("pqnas_workspace_session=") +
                cookieVal
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::Ok,
                cookieVal,
                "canonical workspace session cookie"
            )) {
            return 20;
        }
    }

    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string(
                "theme=dark; pqnas_workspace_session="
            ) +
                cookieVal
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::Ok,
                cookieVal,
                "workspace session with unrelated cookie"
            )) {
            return 21;
        }
    }

    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string("pqnas_session=") +
                cookieVal +
                "; pqnas_workspace_session=" +
                cookieVal
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::Ok,
                cookieVal,
                "workspace and normal sessions coexist"
            )) {
            return 22;
        }
    }

    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string("pqnas_workspace_session=") +
                cookieVal
        );
        request.headers.emplace(
            "Cookie",
            std::string("theme=dark")
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::
                    InvalidCookieHeader,
                "",
                "duplicate Cookie headers for workspace session"
            )) {
            return 23;
        }
    }

    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string("pqnas_workspace_session=") +
                cookieVal +
                "; pqnas_workspace_session=" +
                cookieVal
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::
                    InvalidCookieHeader,
                "",
                "duplicate workspace session names"
            )) {
            return 24;
        }
    }

    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string("pqnas_session=") +
                cookieVal
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::
                    MissingSessionCookie,
                "",
                "missing workspace session cookie"
            )) {
            return 25;
        }
    }

    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string(
                "broken; pqnas_workspace_session="
            ) +
                cookieVal
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::
                    InvalidCookieHeader,
                "",
                "malformed workspace Cookie segment"
            )) {
            return 26;
        }
    }

    {
        httplib::Request request;
        request.headers.emplace(
            "Cookie",
            std::string(
                "pqnas_workspace_session= "
            ) +
                cookieVal
        );

        if (!require_workspace_cookie_result(
                request,
                pqnas::StrictSessionCookieResult::
                    InvalidCookieHeader,
                "",
                "noncanonical workspace session value"
            )) {
            return 27;
        }
    }

    if (!require_allowed(std::string("pqnas_session=") + cookieVal, "canonical session cookie")) return 6;
    if (!require_allowed(std::string("theme=dark; pqnas_session=") + cookieVal, "session cookie with unrelated cookie")) return 7;
    if (!require_allowed(std::string("notpqnas_session=ignored; pqnas_session=") + cookieVal, "exact session cookie name matching")) return 8;

    {
        httplib::Request request;
        request.headers.emplace("Cookie", std::string("pqnas_session=") + cookieVal);
        request.headers.emplace("Cookie", std::string("pqnas_session=") + cookieVal);
        if (!require_rejected(request, "duplicate Cookie headers")) return 9;
    }

    {
        httplib::Request request;
        request.headers.emplace("Cookie", std::string("pqnas_session=") + cookieVal + "; pqnas_session=" + cookieVal);
        if (!require_rejected(request, "duplicate pqnas_session names")) return 10;
    }

    {
        httplib::Request request;
        request.headers.emplace("Cookie", std::string("broken; pqnas_session=") + cookieVal);
        if (!require_rejected(request, "malformed Cookie segment")) return 11;
    }

    {
        httplib::Request request;
        request.headers.emplace("Cookie", std::string("pqnas_session= ") + cookieVal);
        if (!require_rejected(request, "noncanonical session cookie characters")) return 12;
    }

    std::cout << "OK: strict admin cookie/policy regression tests passed\n";
    return 0;
}
