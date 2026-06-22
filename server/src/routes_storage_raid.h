#pragma once

namespace httplib {
class Server;
}

void register_storage_raid_routes(httplib::Server& srv);
