#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace httplib {
class Request;
class Response;
class Server;
}

namespace pqnas::cluster {

class NodeEnrollmentService;

inline constexpr std::size_t
    kAdminNodeEnrollmentRequestBodyMaxBytes =
        8 * 1024;

inline constexpr std::uint64_t
    kAdminNodeEnrollmentMinimumTtlSeconds = 60;

inline constexpr std::uint64_t
    kAdminNodeEnrollmentDefaultTtlSeconds = 15 * 60;

inline constexpr std::uint64_t
    kAdminNodeEnrollmentMaximumTtlSeconds = 60 * 60;

struct AdminNodeEnrollmentRoutesContext {
    NodeEnrollmentService* service = nullptr;

    std::function<bool(
        const httplib::Request&,
        httplib::Response&,
        std::string*
    )> require_admin_auth;

    std::function<bool(
        const httplib::Request&,
        httplib::Response&
    )> require_same_origin;

    std::function<void(
        httplib::Response&,
        int,
        const std::string&
    )> reply_json;

    std::function<std::string()>
        now_iso_utc;

    // Adds seconds to the supplied timestamp. Injection keeps route tests
    // deterministic and prevents this module from introducing another clock.
    std::function<bool(
        const std::string&,
        std::uint64_t,
        std::string*
    )> add_seconds_iso_utc;
};

// Exposed separately so authentication, strict parsing and secret response
// behavior can be tested without starting an HTTP listener.
void handle_admin_node_enrollments_list(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminNodeEnrollmentRoutesContext& context
);

void handle_admin_node_enrollments_create(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminNodeEnrollmentRoutesContext& context
);

void handle_admin_node_enrollments_revoke(
    const httplib::Request& req,
    httplib::Response& res,
    const AdminNodeEnrollmentRoutesContext& context
);

void register_admin_node_enrollment_routes(
    httplib::Server& server,
    const AdminNodeEnrollmentRoutesContext& context
);

} // namespace pqnas::cluster
