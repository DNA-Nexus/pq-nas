#pragma once

#include "httplib.h"
#include "users_registry.h"

#include <functional>
#include <string>

namespace pqnas::notifications {

struct NotificationRoutesDeps {
    UsersRegistry* users = nullptr;
    const unsigned char* cookie_key = nullptr;

    std::function<bool(const httplib::Request&,
                       httplib::Response&,
                       const unsigned char*,
                       UsersRegistry*,
                       std::string*,
                       std::string*)> require_user_auth_users_actor;

    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
};

void register_notification_routes(httplib::Server& srv, const NotificationRoutesDeps& deps);

} // namespace pqnas::notifications
