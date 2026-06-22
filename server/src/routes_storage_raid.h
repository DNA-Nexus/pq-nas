#pragma once

#include <string>

namespace httplib {
class Server;
}

struct StorageRaidRoutesContext {
    std::string cookie_key;
    std::string users_path;
    std::string workspaces_path;
    std::string data_root_dir;
};

void register_storage_raid_routes(
    httplib::Server& srv,
    const StorageRaidRoutesContext& ctx
);
