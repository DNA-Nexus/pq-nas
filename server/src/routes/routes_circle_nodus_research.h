#pragma once

#include "httplib.h"
#include "users_registry.h"

#include <functional>
#include <string>

namespace pqnas {

struct CircleNodusResearchRoutesDeps {
    UsersRegistry* users = nullptr;
    const unsigned char* cookie_key = nullptr;

    std::function<bool(const httplib::Request&,
                       httplib::Response&,
                       const unsigned char*,
                       UsersRegistry*,
                       std::string*,
                       std::string*)> require_user_auth_users_actor;
};

void register_circle_nodus_research_routes(
    httplib::Server& server,
    const CircleNodusResearchRoutesDeps& deps);

} // namespace pqnas
