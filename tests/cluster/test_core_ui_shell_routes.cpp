#include "routes/routes_core_ui_shell.h"

#include "httplib.h"

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
    const auto iterator =
        response.headers.find(name);

    if (iterator == response.headers.end()) {
        return {};
    }

    return iterator->second;
}

CoreUiShellRoutesContext make_context(
    bool is_director,
    bool* auth_allowed,
    bool* admin_session,
    int* auth_calls,
    int* admin_checks,
    int* slurp_calls
) {
    CoreUiShellRoutesContext context;

    context.is_director = is_director;
    context.is_storage_node = false;

    context.static_system_js = "system.js";
    context.static_audit_html = "audit.html";
    context.static_admin_html = "admin.html";
    context.static_director_admin_html =
        "director/admin.html";
    context.static_director_admin_css =
        "director/admin.css";
    context.static_director_admin_js =
        "director/admin.js";
    context.static_app_html = "app.html";
    context.static_app_js = "app.js";
    context.static_app_frame_background_tasks_js =
        "app_frame_background_tasks.js";
    context.static_admin_js = "admin.js";

    context.static_handoff_director_js =
        "handoff/director.js";
    context.static_handoff_storage_bootstrap_js =
        "handoff/storage_bootstrap.js";
    context.static_handoff_storage_finish_js =
        "handoff/storage_finish.js";

    context.require_admin =
        [auth_allowed, auth_calls](
            const httplib::Request&,
            httplib::Response& response,
            std::string* actor_fp
        ) {
            ++(*auth_calls);

            if (!*auth_allowed) {
                response.status = 401;
                response.set_content(
                    "unauthorized",
                    "text/plain; charset=utf-8"
                );
                return false;
            }

            if (actor_fp) {
                *actor_fp = "admin-fingerprint";
            }

            return true;
        };

    context.require_user =
        [auth_allowed, auth_calls](
            const httplib::Request&,
            httplib::Response& response,
            std::string* actor_fp
        ) {
            ++(*auth_calls);

            if (!*auth_allowed) {
                response.status = 401;
                response.set_content(
                    "unauthorized",
                    "text/plain; charset=utf-8"
                );
                return false;
            }

            if (actor_fp) {
                *actor_fp = "user-fingerprint";
            }

            return true;
        };

    context.is_admin =
        [admin_session, admin_checks](
            const httplib::Request&
        ) {
            ++(*admin_checks);
            return *admin_session;
        };

    context.read_file_to_string =
        [](
            const std::string&,
            std::string& body
        ) {
            body = "STATIC";
            return true;
        };

    context.slurp_file =
        [slurp_calls](
            const std::string& path
        ) {
            ++(*slurp_calls);

            if (path == "admin.html") {
                return std::string("NORMAL_ADMIN");
            }

            if (path == "director/admin.html") {
                return std::string("DIRECTOR_ADMIN");
            }

            if (path == "app.html") {
                return std::string("NORMAL_APP");
            }

            return std::string("OTHER_STATIC");
        };

    return context;
}

} // namespace

int main() {
    bool auth_allowed = true;
    bool admin_session = false;

    int auth_calls = 0;
    int admin_checks = 0;
    int slurp_calls = 0;

    httplib::Request request;

    {
        auto context =
            make_context(
                false,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        httplib::Response response;

        handle_core_ui_app(
            request,
            response,
            context
        );

        expect(
            response.status == 200,
            "standalone /app should return 200"
        );

        expect(
            response.body == "NORMAL_APP",
            "standalone /app should serve normal app HTML"
        );

        expect(
            admin_checks == 0,
            "standalone /app should not perform Director admin check"
        );
    }

    {
        auth_allowed = true;
        admin_session = true;
        auth_calls = 0;
        admin_checks = 0;
        slurp_calls = 0;

        auto context =
            make_context(
                true,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        httplib::Response response;

        handle_core_ui_app(
            request,
            response,
            context
        );

        expect(
            response.status == 302,
            "Director admin /app should redirect"
        );

        expect(
            response_header(
                response,
                "Location"
            ) == "/admin",
            "Director admin /app should redirect to /admin"
        );

        expect(
            admin_checks == 1,
            "Director /app should verify admin session"
        );

        expect(
            slurp_calls == 0,
            "Director admin /app must not load NAS app HTML"
        );
    }

    {
        auth_allowed = true;
        admin_session = false;
        auth_calls = 0;
        admin_checks = 0;
        slurp_calls = 0;

        auto context =
            make_context(
                true,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        httplib::Response response;

        handle_core_ui_app(
            request,
            response,
            context
        );

        expect(
            response.status == 302,
            "Director normal user /app should enter Storage handoff"
        );

        expect(
            response_header(
                response,
                "Location"
            ) == "/storage/handoff/director",
            "Director normal user /app should use fixed handoff route"
        );

        expect(
            admin_checks == 1,
            "Director normal user /app should verify admin status"
        );

        expect(
            auth_calls == 1,
            "Director normal user /app should require user authentication"
        );

        expect(
            slurp_calls == 0,
            "Director normal user /app must never load NAS app HTML"
        );

        expect(
            response_header(
                response,
                "Cache-Control"
            ) == "no-store",
            "Director handoff redirect should disable caching"
        );
    }

    {
        auth_allowed = true;
        admin_session = true;
        auth_calls = 0;
        admin_checks = 0;
        slurp_calls = 0;

        auto context =
            make_context(
                true,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        httplib::Response response;

        handle_core_ui_admin(
            request,
            response,
            context
        );

        expect(
            response.status == 200,
            "Director admin /admin should return 200"
        );

        expect(
            response.body == "DIRECTOR_ADMIN",
            "Director /admin should serve Director HTML"
        );

        expect(
            auth_calls == 1,
            "Director /admin should require admin authentication"
        );
    }

    {
        auth_allowed = true;
        admin_session = true;
        auth_calls = 0;
        admin_checks = 0;
        slurp_calls = 0;

        auto context =
            make_context(
                false,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        httplib::Response response;

        handle_core_ui_admin(
            request,
            response,
            context
        );

        expect(
            response.status == 200,
            "standalone admin /admin should return 200"
        );

        expect(
            response.body == "NORMAL_ADMIN",
            "standalone /admin should preserve existing admin HTML"
        );

        expect(
            auth_calls == 1,
            "standalone /admin should retain admin authentication"
        );
    }

    {
        auth_allowed = false;
        admin_session = false;
        auth_calls = 0;
        admin_checks = 0;
        slurp_calls = 0;

        auto context =
            make_context(
                true,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        httplib::Response response;

        handle_core_ui_admin(
            request,
            response,
            context
        );

        expect(
            response.status == 401,
            "unauthorized Director /admin should return auth failure"
        );

        expect(
            auth_calls == 1,
            "unauthorized Director /admin should run auth exactly once"
        );

        expect(
            slurp_calls == 0,
            "unauthorized Director /admin must not load Director HTML"
        );
    }


    {
        auth_allowed = true;
        admin_session = false;
        auth_calls = 0;
        admin_checks = 0;
        slurp_calls = 0;

        auto context =
            make_context(
                true,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        httplib::Response response;

        handle_core_ui_handoff_director(
            request,
            response,
            context
        );

        expect(
            response.status == 200,
            "authenticated Director handoff page should be served"
        );

        expect(
            response.body.find(
                "/static/handoff/director.js"
            ) != std::string::npos,
            "Director handoff page should load only its external script"
        );

        expect(
            response_header(
                response,
                "Content-Security-Policy"
            ).find(
                "default-src 'none'"
            ) != std::string::npos &&
            response_header(
                response,
                "Content-Security-Policy"
            ).find(
                "frame-ancestors 'none'"
            ) != std::string::npos,
            "Director handoff page should have strict CSP"
        );

        expect(
            response_header(
                response,
                "Referrer-Policy"
            ) == "no-referrer",
            "handoff page should never leak capability fragment via referrer"
        );
    }

    {
        auth_allowed = true;
        admin_session = false;
        auth_calls = 0;
        admin_checks = 0;
        slurp_calls = 0;

        auto context =
            make_context(
                false,
                &auth_allowed,
                &admin_session,
                &auth_calls,
                &admin_checks,
                &slurp_calls
            );

        context.is_storage_node = true;

        httplib::Response bootstrap_response;

        handle_core_ui_handoff_storage_bootstrap(
            request,
            bootstrap_response,
            context
        );

        expect(
            bootstrap_response.status == 200 &&
            bootstrap_response.body.find(
                "/static/handoff/storage_bootstrap.js"
            ) != std::string::npos,
            "Storage Node should serve bootstrap handoff page"
        );

        httplib::Response finish_response;

        handle_core_ui_handoff_storage_finish(
            request,
            finish_response,
            context
        );

        expect(
            finish_response.status == 200 &&
            finish_response.body.find(
                "/static/handoff/storage_finish.js"
            ) != std::string::npos,
            "Storage Node should serve finish handoff page"
        );

        expect(
            auth_calls == 0,
            "Storage bootstrap/finish pages must not depend on Director cookie"
        );
    }

    if (failures != 0) {
        std::cerr
            << failures
            << " core UI shell route test(s) failed\n";
        return 1;
    }

    std::cout
        << "OK: core UI shell route tests passed\n";

    return 0;
}
