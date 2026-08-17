#include "interactive_user_login_policy.h"
#include "routes_v5.h"
#include "verify_login_common.h"

#include <httplib.h>

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

std::string response_header(
    const httplib::Response& response,
    const std::string& name
) {
    const auto found =
        response.headers.find(name);

    if (found == response.headers.end()) {
        return {};
    }

    return found->second;
}

} // namespace

int main() {
    {
        RoutesV5Context context;

        expect(
            !context.interactive_user_login_allowed,
            "RoutesV5Context defaults interactive login to denied"
        );
    }

    {
        VerifyLoginCommonContext context;

        expect(
            !context.interactive_user_login_allowed,
            "VerifyLoginCommonContext defaults interactive login to denied"
        );
    }

    {
        httplib::Response response;

        const bool allowed =
            pqnas::require_interactive_user_login(
                false,
                response
            );

        expect(
            !allowed,
            "disabled policy denies request"
        );

        expect(
            response.status == 403,
            "disabled policy returns 403"
        );

        expect(
            response_header(
                response,
                "Content-Type"
            ) == "application/json; charset=utf-8",
            "disabled policy returns JSON"
        );

        expect(
            response_header(
                response,
                "Cache-Control"
            ) == "no-store",
            "disabled policy response is not cacheable"
        );

        expect(
            response.body ==
                R"({"ok":false,"error":"interactive_login_disabled"})",
            "disabled policy returns stable error contract"
        );
    }

    {
        httplib::Response response;

        response.status = 418;
        response.body = "unchanged";

        const bool allowed =
            pqnas::require_interactive_user_login(
                true,
                response
            );

        expect(
            allowed,
            "enabled policy allows request"
        );

        expect(
            response.status == 418 &&
            response.body == "unchanged",
            "enabled policy does not mutate response"
        );
    }

    if (failures != 0) {
        std::cerr
            << "FAIL: "
            << failures
            << " interactive user login policy test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: interactive user login policy tests passed\n";

    return 0;
}
