#include "interactive_user_login_policy.h"

namespace pqnas {

bool require_interactive_user_login(
    bool interactive_user_login_allowed,
    httplib::Response& response
) {
    if (interactive_user_login_allowed) {
        return true;
    }

    // Security: missing/disabled interactive-login permission must fail closed.
    response.status = 403;
    response.set_header(
        "Content-Type",
        "application/json; charset=utf-8"
    );
    response.set_header(
        "Cache-Control",
        "no-store"
    );
    response.body =
        R"({"ok":false,"error":"interactive_login_disabled"})";

    return false;
}

} // namespace pqnas
