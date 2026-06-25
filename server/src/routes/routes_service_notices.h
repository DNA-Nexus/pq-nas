#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "service_notices.h"

namespace httplib {
class Server;
struct Request;
struct Response;
}

namespace pqnas {

struct AuditEvent;

struct ServiceNoticeRoutesDeps {
    ServiceNoticesStore* store = nullptr;

    std::string static_admin_service_notices_html;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*, std::string*)>
        require_user;

    std::function<bool(const httplib::Request&, httplib::Response&, std::string*)>
        require_admin;

    std::function<bool(const httplib::Request&, httplib::Response&)>
        require_same_origin;

    std::function<void(httplib::Response&, int, const std::string&)>
        reply_json;

    std::function<void(const AuditEvent&)>
        audit_append;

    std::function<std::int64_t()>
        now_epoch;
};

void register_service_notice_routes(httplib::Server& srv,
                                    const ServiceNoticeRoutesDeps& deps);

} // namespace pqnas
