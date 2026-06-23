#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
}

namespace pqnas {
struct AuditEvent;
}

struct StorageRaidRoutesContext {
    const unsigned char* cookie_key = nullptr;
    std::string users_path;
    std::string workspaces_path;
    std::function<void(const pqnas::AuditEvent&)> audit_append;
};

void register_storage_raid_routes(
    httplib::Server& srv,
    const StorageRaidRoutesContext& ctx
);
