#pragma once

#include <functional>
#include <string>

namespace pqnas::cluster {

class StorageHandoffTicketStore;
class StorageUserSessionPolicy;

enum class StorageHandoffConsumeServiceResult {
    Ok,
    InvalidInput,
    InvalidConfiguration,

    // Security: ticket/projection/revision failures intentionally collapse
    // into one result so a future browser route need not expose an
    // authorization-state oracle.
    Rejected,

    ClockUnavailable,
    SessionMintFailed,
    InternalError
};

const char*
storage_handoff_consume_service_result_code(
    StorageHandoffConsumeServiceResult result
) noexcept;

using StorageHandoffConsumeClock =
    std::function<long()>;

struct StorageHandoffConsumeServiceConfig {
    StorageHandoffTicketStore* ticket_store =
        nullptr;

    const StorageUserSessionPolicy*
        session_policy = nullptr;

    // Must point to exactly 32 bytes and outlive this service.
    //
    // Security: the Storage cookie primitive domain-separates this base key
    // before signing; the base key itself is never used directly as the
    // Storage session signing key.
    const unsigned char* base_cookie_key =
        nullptr;

    std::string local_node_id;

    // Explicit production configuration. No hidden default is applied here.
    // The Storage cookie primitive also enforces its hard security ceiling.
    long session_ttl_seconds = 0;

    StorageHandoffConsumeClock clock;
};

struct StorageHandoffConsumeServiceOutput {
    std::string session_cookie_value;
    long expires_at = 0;
};

class StorageHandoffConsumeService {
public:
    explicit StorageHandoffConsumeService(
        StorageHandoffConsumeServiceConfig config
    );

    // Security ordering:
    //
    // 1. atomically verify browser binding + consume the one-time ticket
    // 2. authorize against the current local projection
    // 3. require exact ticket/projection Node + revision binding
    // 4. mint a Node-domain-separated Storage session cookie
    //
    // Once ticket consumption succeeds, every later failure leaves the
    // ticket permanently spent. Bearers are never restored or re-issued.
    StorageHandoffConsumeServiceResult consume(
        const std::string& ticket,
        const std::string& browser_binding_secret,
        StorageHandoffConsumeServiceOutput* output
    ) const noexcept;

private:
    bool config_valid() const noexcept;
    long now_epoch_safe() const noexcept;

    StorageHandoffConsumeServiceConfig config_;
};

} // namespace pqnas::cluster
