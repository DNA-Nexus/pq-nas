#pragma once

#include "httplib.h"
#include "users_registry.h"

#include <filesystem>
#include <functional>
#include <string>

namespace pqnas {

struct CircleStackRoutesDeps {
    UsersRegistry* users = nullptr;
    const unsigned char* cookie_key = nullptr;

    std::function<std::filesystem::path(UsersRegistry&, const std::string&)> user_dir_for_fp;

    std::function<bool(const httplib::Request&,
                       httplib::Response&,
                       const unsigned char*,
                       UsersRegistry*,
                       std::string*,
                       std::string*)> require_user_auth_users_actor;
};

void register_circle_stack_routes(httplib::Server& server, const CircleStackRoutesDeps& deps);

} // namespace pqnas
