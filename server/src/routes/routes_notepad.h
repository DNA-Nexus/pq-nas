#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {
class NotepadStore;
class UsersRegistry;

struct NotepadRoutesDeps {
    NotepadStore* store = nullptr;
    UsersRegistry* users = nullptr;
    const unsigned char* cookie_key = nullptr;

    std::function<bool(const httplib::Request&,
                       httplib::Response&,
                       const unsigned char*,
                       UsersRegistry*,
                       std::string*,
                       std::string*)> require_user_auth_users_actor;

    std::function<bool(const httplib::Request&, httplib::Response&)> require_same_origin;
    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
    std::function<std::int64_t()> now_epoch_sec;

    // Resolves app-private data into the user's own storage root so snapshots
    // and storage-pool migration carry Notepad content with the user.
    std::function<std::filesystem::path(const std::string&)> user_dir_for_fp;
};

void register_notepad_routes(httplib::Server& srv, const NotepadRoutesDeps& deps);

} // namespace pqnas
