#include "routes/routes_workspace_external_sessions.h"
#include "workspace_access_shared.h"
#include "workspace_external_sessions.h"
#include "workspaces.h"

#include <httplib.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace fs = std::filesystem;

// Test-local implementation for the one workspace-access primitive used by
// routes_workspace_external_sessions.cpp. This keeps the handler test focused
// on FINAL-002 without pulling the full filesystem/storage resolver graph into
// the test binary.
namespace pqnas {

std::optional<WorkspaceMemberRec>
workspace_enabled_member_for_actor(
    const WorkspaceRec& workspace,
    const std::string& actor_fingerprint
) {
    for (const auto& member : workspace.members) {
        if (member.fingerprint == actor_fingerprint &&
            member.status == "enabled") {
            return member;
        }
    }

    return std::nullopt;
}

} // namespace pqnas

namespace {

bool expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        std::cerr
            << "FAIL: "
            << message
            << "\n";
        return false;
    }

    return true;
}

} // namespace

int main() {
    bool ok = true;

    const auto unique =
        std::chrono::steady_clock::now().
            time_since_epoch().
            count();

    const fs::path test_directory =
        fs::temp_directory_path() /
        (
            "pqnas-final002-workspace-session-" +
            std::to_string(unique)
        );

    std::error_code ec;

    fs::create_directories(
        test_directory,
        ec
    );

    ok &= expect(
        !ec,
        "create temporary test directory"
    );

    const fs::path workspaces_path =
        test_directory /
        "workspaces.json";

    pqnas::WorkspacesRegistry workspaces;

    pqnas::WorkspaceRec workspace;
    workspace.workspace_id =
        pqnas::new_workspace_id();
    workspace.name =
        "FINAL-002";
    workspace.status =
        "enabled";
    workspace.kind =
        "admin";

    const std::string external_fingerprint(
        128,
        'a'
    );

    pqnas::WorkspaceMemberRec member;
    member.fingerprint =
        external_fingerprint;
    member.role =
        "viewer";
    member.status =
        "enabled";
    member.member_kind =
        "external";
    member.display_name =
        "FINAL-002 external";

    workspace.members.push_back(
        member
    );

    ok &= expect(
        workspaces.upsert(
            workspace
        ),
        "insert workspace fixture"
    );

    ok &= expect(
        workspaces.save(
            workspaces_path.string()
        ),
        "persist workspace fixture"
    );

    pqnas::WorkspaceExternalSessionsStore
        external_sessions;

    pqnas::WorkspaceExternalSessionRec
        session;

    session.session_id =
        pqnas::
            new_workspace_external_session_id();

    session.workspace_id =
        workspace.workspace_id;

    session.st_hash_b64 =
        "final002-st-hash";

    session.st_token =
        "final002-st-token";

    session.status =
        "pending";

    session.created_at =
        "2026-08-26T00:00:00Z";

    constexpr long now_epoch =
        1'800'000'000L;

    session.expires_at_epoch =
        now_epoch + 600;

    ok &= expect(
        external_sessions.upsert(
            session
        ),
        "insert external session fixture"
    );

    ok &= expect(
        external_sessions.mark_approved(
            session.session_id,
            external_fingerprint,
            "viewer",
            "2026-08-26T00:00:01Z"
        ),
        "approve external session fixture"
    );

    const std::string origin =
        "https://final002.example";

    const std::string app =
        "DNA-Nexus FINAL-002 test";

    std::array<unsigned char, 32>
        cookie_key{};

    std::size_t mint_calls = 0;

    pqnas::WorkspaceExternalSessionRouteDeps
        deps;

    deps.workspaces =
        &workspaces;

    deps.external_sessions =
        &external_sessions;

    deps.workspaces_path =
        workspaces_path.string();

    deps.origin =
        &origin;

    deps.app =
        &app;

    deps.cookie_key =
        cookie_key.data();

    // FINAL-002 security default for the first request: model role=node.
    deps.interactive_user_login_allowed =
        false;

    deps.session_cookie_mint =
        [&](
            const unsigned char*,
            const std::string&,
            long,
            long,
            std::string& output
        ) -> bool {
            ++mint_calls;
            output =
                "final002-minted-cookie";
            return true;
        };

    deps.b64_std =
        [](
            const unsigned char*,
            std::size_t
        ) {
            return
                std::string(
                    "final002-claim-b64"
                );
        };

    deps.reply_json =
        [](
            httplib::Response& response,
            int status,
            const std::string& body
        ) {
            response.status =
                status;

            response.set_header(
                "Content-Type",
                "application/json; charset=utf-8"
            );

            response.body =
                body;
        };

    deps.now_epoch_sec =
        []() -> std::int64_t {
            return now_epoch;
        };

    deps.now_iso_utc =
        []() {
            return
                std::string(
                    "2026-08-26T00:00:02Z"
                );
        };

    httplib::Server server;

    pqnas::
        register_workspace_external_session_routes(
            server,
            deps
        );

    const int port =
        server.bind_to_any_port(
            "127.0.0.1"
        );

    ok &= expect(
        port > 0,
        "bind local handler test server"
    );

    if (port <= 0) {
        fs::remove_all(
            test_directory,
            ec
        );

        return 1;
    }

    std::thread server_thread(
        [&]() {
            server.listen_after_bind();
        }
    );

    std::this_thread::sleep_for(
        std::chrono::milliseconds(50)
    );

    httplib::Client client(
        "127.0.0.1",
        port
    );

    const std::string request_body =
        std::string(
            R"({"session_id":")"
        ) +
        session.session_id +
        R"("})";

    httplib::Headers headers{
        {
            "Origin",
            origin
        }
    };

    // ------------------------------------------------------------
    // Node role: credential mint must fail before consuming state.
    // ------------------------------------------------------------

    const auto denied =
        client.Post(
            "/api/v4/workspaces/external-sessions/consume",
            headers,
            request_body,
            "application/json"
        );

    ok &= expect(
        static_cast<bool>(denied),
        "Node consume request returns HTTP response"
    );

    if (denied) {
        ok &= expect(
            denied->status == 403,
            "Node consume is denied with HTTP 403"
        );

        ok &= expect(
            denied->body.find(
                "interactive_login_disabled"
            ) != std::string::npos,
            "Node consume reports interactive_login_disabled"
        );

        ok &= expect(
            denied->get_header_value(
                "Set-Cookie"
            ).empty(),
            "Node consume does not mint workspace cookie"
        );
    }

    ok &= expect(
        mint_calls == 0,
        "Node consume never calls workspace cookie mint"
    );

    const auto after_denied =
        external_sessions.get(
            session.session_id
        );

    ok &= expect(
        after_denied.has_value() &&
        after_denied->status ==
            "approved",
        "Node denial does not consume approved session"
    );

    // ------------------------------------------------------------
    // Standalone role: same approved session and same HTTP request
    // must continue through the normal handler and mint the cookie.
    // ------------------------------------------------------------

    deps.interactive_user_login_allowed =
        true;

    const auto allowed =
        client.Post(
            "/api/v4/workspaces/external-sessions/consume",
            headers,
            request_body,
            "application/json"
        );

    ok &= expect(
        static_cast<bool>(allowed),
        "standalone consume request returns HTTP response"
    );

    if (allowed) {
        ok &= expect(
            allowed->status == 200,
            "standalone consume succeeds"
        );

        ok &= expect(
            allowed->body.find(
                R"("ok":true)"
            ) != std::string::npos,
            "standalone consume returns ok"
        );

        ok &= expect(
            allowed->get_header_value(
                "Set-Cookie"
            ).find(
                "pqnas_workspace_session="
                "final002-minted-cookie"
            ) != std::string::npos,
            "standalone consume mints workspace cookie"
        );
    }

    ok &= expect(
        mint_calls == 1,
        "standalone consume calls cookie mint exactly once"
    );

    const auto after_allowed =
        external_sessions.get(
            session.session_id
        );

    ok &= expect(
        after_allowed.has_value() &&
        after_allowed->status ==
            "consumed",
        "successful standalone consume atomically consumes session"
    );

    server.stop();

    if (server_thread.joinable()) {
        server_thread.join();
    }

    fs::remove_all(
        test_directory,
        ec
    );

    if (!ok) {
        return 1;
    }

    std::cout
        << "OK: FINAL-002 workspace external-session role gate tests passed\n";

    return 0;
}
