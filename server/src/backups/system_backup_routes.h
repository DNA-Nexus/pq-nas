#pragma once

#include "httplib.h"
#include "users_registry.h"
#include "backups/system_backup_worker.h"

#include <functional>
#include <string>

namespace pqnas::backups {

struct SystemBackupRoutesDeps {
    UsersRegistry* users = nullptr;
    const unsigned char* cookie_key = nullptr;
    SystemBackupWorker* worker = nullptr;

    std::function<bool(const httplib::Request&,
                       httplib::Response&,
                       const unsigned char*,
                       UsersRegistry*,
                       std::string*,
                       std::string*)> require_user_auth_users_actor;

    std::function<void(httplib::Response&, int, const std::string&)> reply_json;
};

void register_system_backup_routes(httplib::Server& srv, const SystemBackupRoutesDeps& deps);

} // namespace pqnas::backups
