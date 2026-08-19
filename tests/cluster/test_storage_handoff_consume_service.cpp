#include "cluster/storage_handoff_consume_service.h"

#include "cluster/storage_handoff_ticket_store.h"
#include "cluster/storage_user_session_cookie.h"
#include "cluster/storage_user_session_policy.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

using pqnas::cluster::
    StorageHandoffConsumeClock;

using pqnas::cluster::
    StorageHandoffConsumeService;

using pqnas::cluster::
    StorageHandoffConsumeServiceConfig;

using pqnas::cluster::
    StorageHandoffConsumeServiceOutput;

using pqnas::cluster::
    StorageHandoffConsumeServiceResult;

using pqnas::cluster::
    StorageHandoffTicketResult;

using pqnas::cluster::
    StorageHandoffTicketStore;

using pqnas::cluster::
    StorageUserProjection;

using pqnas::cluster::
    StorageUserSessionCookieVerifyResult;

using pqnas::cluster::
    StorageUserSessionPolicy;

int failures = 0;

void expect(
    bool condition,
    const std::string& message
) {
    if (!condition) {
        ++failures;

        std::cerr
            << "FAIL: "
            << message
            << '\n';
    }
}

void expect_result(
    StorageHandoffConsumeServiceResult actual,
    StorageHandoffConsumeServiceResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                storage_handoff_consume_service_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                storage_handoff_consume_service_result_code(
                    actual
                )
    );
}

StorageUserProjection valid_projection(
    const std::string& fingerprint,
    std::uint64_t revision = 17
) {
    StorageUserProjection projection;

    projection.fingerprint =
        fingerprint;

    projection.enabled = true;
    projection.storage_allocated = true;
    projection.quota_bytes = 1024;

    projection.root_rel =
        "users/example";

    projection.storage_pool_id.clear();

    projection.home_node =
        "storage-01";

    projection.revision =
        revision;

    return projection;
}

} // namespace

int main() {
    const std::string fingerprint(
        128,
        'a'
    );

    const std::string local_node =
        "storage-01";

    // Security: handoff tickets are redeemable only by the browser
    // that owns the matching pre-auth binding secret.
    std::string browser_binding_secret(
        pqnas::cluster::
            kStorageHandoffBrowserBindingChars,
        'A'
    );

    std::string wrong_browser_binding_secret(
        pqnas::cluster::
            kStorageHandoffBrowserBindingChars,
        'A'
    );

    wrong_browser_binding_secret.back() =
        'E';

    std::string browser_binding_hash;

    expect(
        pqnas::cluster::
            storage_handoff_browser_binding_hash(
                browser_binding_secret,
                &browser_binding_hash
            ),
        "consume fixture browser binding hashes"
    );

    std::int64_t ticket_now = 1000;

    StorageHandoffTicketStore ticket_store(
        128,
        [&]() -> std::int64_t {
            return ticket_now;
        }
    );

    std::optional<StorageUserProjection>
        current_projection =
            valid_projection(
                fingerprint
            );

    StorageUserSessionPolicy::Config
        policy_config;

    policy_config.local_node_id =
        local_node;

    policy_config.lookup_projection =
        [&](const std::string& candidate)
            -> std::optional<StorageUserProjection> {
            if (!current_projection.has_value() ||
                current_projection->fingerprint !=
                    candidate) {
                return std::nullopt;
            }

            return current_projection;
        };

    StorageUserSessionPolicy policy(
        std::move(policy_config)
    );

    std::array<unsigned char, 32>
        base_key{};

    for (std::size_t i = 0;
         i < base_key.size();
         ++i) {
        base_key[i] =
            static_cast<unsigned char>(
                i + 1
            );
    }

    long session_now = 2000;

    auto make_config =
        [&](const unsigned char* key,
            const StorageUserSessionPolicy*
                selected_policy,
            StorageHandoffConsumeClock clock,
            long ttl)
            -> StorageHandoffConsumeServiceConfig {
            StorageHandoffConsumeServiceConfig
                config;

            config.ticket_store =
                &ticket_store;

            config.session_policy =
                selected_policy;

            config.base_cookie_key =
                key;

            config.local_node_id =
                local_node;

            config.session_ttl_seconds =
                ttl;

            config.clock =
                std::move(clock);

            return config;
        };

    StorageHandoffConsumeService service(
        make_config(
            base_key.data(),
            &policy,
            [&]() -> long {
                return session_now;
            },
            600
        )
    );

    auto issue_ticket =
        [&](const std::string& home_node,
            std::uint64_t revision,
            const std::string& message)
            -> std::string {
            std::string ticket;

            const StorageHandoffTicketResult
                result =
                    ticket_store.issue(
                        fingerprint,
                        home_node,
                        revision,
                        browser_binding_hash,
                        30,
                        &ticket,
                        nullptr
                    );

            expect(
                result ==
                    StorageHandoffTicketResult::Ok,
                message + " issues"
            );

            expect(
                !ticket.empty(),
                message + " returns plaintext ticket"
            );

            return ticket;
        };


    // ------------------------------------------------------------------
    // Valid redemption + replay rejection.
    // ------------------------------------------------------------------
    std::string ticket =
        issue_ticket(
            local_node,
            17,
            "valid ticket"
        );

    StorageHandoffConsumeServiceOutput
        output;

    expect_result(
        service.consume(
            ticket,
            wrong_browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "wrong browser binding rejects handoff"
    );

    expect(
        ticket_store.size() == 1,
        "wrong browser binding does not burn handoff ticket"
    );

    expect(
        output.session_cookie_value.empty() &&
        output.expires_at == 0,
        "wrong browser binding returns no session"
    );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Ok,
        "valid ticket creates Storage session"
    );

    expect(
        !output.session_cookie_value.empty() &&
        output.expires_at == 2600,
        "valid consume returns bounded session cookie"
    );

    std::string verified_fp;
    std::uint64_t verified_revision = 0;
    long verified_exp = 0;

    expect(
        pqnas::cluster::
            storage_user_session_cookie_verify(
                base_key.data(),
                local_node,
                output.session_cookie_value,
                2100,
                &verified_fp,
                &verified_revision,
                &verified_exp
            ) ==
            StorageUserSessionCookieVerifyResult::
                Verified,
        "consume output is a valid Storage-domain cookie"
    );

    expect(
        verified_fp == fingerprint &&
        verified_revision == 17 &&
        verified_exp == 2600,
        "consume cookie carries expected identity revision and expiry"
    );

    output.session_cookie_value =
        "stale-secret";

    output.expires_at = 9999;

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "same ticket cannot be redeemed twice"
    );

    expect(
        output.session_cookie_value.empty() &&
        output.expires_at == 0,
        "replay failure clears prior session output"
    );


    // ------------------------------------------------------------------
    // Missing projection burns the already-consumed ticket.
    // ------------------------------------------------------------------
    ticket =
        issue_ticket(
            local_node,
            17,
            "missing-projection ticket"
        );

    current_projection.reset();

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "missing projection rejects consumed ticket"
    );

    current_projection =
        valid_projection(
            fingerprint
        );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "missing-projection failure does not restore ticket"
    );


    // ------------------------------------------------------------------
    // Disabled projection.
    // ------------------------------------------------------------------
    ticket =
        issue_ticket(
            local_node,
            17,
            "disabled ticket"
        );

    current_projection =
        valid_projection(
            fingerprint
        );

    current_projection->enabled =
        false;

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "disabled projection rejects handoff"
    );


    // ------------------------------------------------------------------
    // Unallocated projection.
    // ------------------------------------------------------------------
    ticket =
        issue_ticket(
            local_node,
            17,
            "unallocated ticket"
        );

    current_projection =
        valid_projection(
            fingerprint
        );

    current_projection->
        storage_allocated = false;

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "unallocated projection rejects handoff"
    );


    // ------------------------------------------------------------------
    // Ticket minted for another Node.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint
        );

    ticket =
        issue_ticket(
            "storage-02",
            17,
            "foreign-node ticket"
        );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "foreign Node ticket rejects locally"
    );


    // ------------------------------------------------------------------
    // Exact revision binding: newer current projection invalidates ticket.
    // ------------------------------------------------------------------
    ticket =
        issue_ticket(
            local_node,
            17,
            "old-revision ticket"
        );

    current_projection =
        valid_projection(
            fingerprint,
            18
        );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "rev17 ticket cannot authorize rev18 projection"
    );


    // ------------------------------------------------------------------
    // Exact revision binding: older current projection also invalid.
    // ------------------------------------------------------------------
    ticket =
        issue_ticket(
            local_node,
            18,
            "future-revision ticket"
        );

    current_projection =
        valid_projection(
            fingerprint,
            17
        );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "rev18 ticket cannot authorize rev17 projection"
    );


    // ------------------------------------------------------------------
    // Projection lookup failure must fail closed and burn the ticket.
    // ------------------------------------------------------------------
    StorageUserSessionPolicy::Config
        throwing_policy_config;

    throwing_policy_config.local_node_id =
        local_node;

    throwing_policy_config.lookup_projection =
        [](const std::string&)
            -> std::optional<StorageUserProjection> {
            throw std::runtime_error(
                "synthetic projection lookup failure"
            );
        };

    StorageUserSessionPolicy throwing_policy(
        std::move(
            throwing_policy_config
        )
    );

    StorageHandoffConsumeService
        throwing_service(
            make_config(
                base_key.data(),
                &throwing_policy,
                [&]() -> long {
                    return session_now;
                },
                600
            )
        );

    ticket =
        issue_ticket(
            local_node,
            17,
            "lookup-failure ticket"
        );

    expect_result(
        throwing_service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "projection lookup failure rejects handoff"
    );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "lookup failure leaves ticket permanently spent"
    );


    // ------------------------------------------------------------------
    // Cookie mint failure happens after the one-time replay boundary.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint
        );

    std::array<unsigned char, 32>
        zero_key{};

    StorageHandoffConsumeService
        zero_key_service(
            make_config(
                zero_key.data(),
                &policy,
                [&]() -> long {
                    return session_now;
                },
                600
            )
        );

    ticket =
        issue_ticket(
            local_node,
            17,
            "mint-failure ticket"
        );

    output.session_cookie_value =
        "stale-secret";

    output.expires_at = 9999;

    expect_result(
        zero_key_service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            SessionMintFailed,
        "cookie mint failure fails closed"
    );

    expect(
        output.session_cookie_value.empty() &&
        output.expires_at == 0,
        "cookie mint failure clears output"
    );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "cookie mint failure does not restore ticket"
    );


    // ------------------------------------------------------------------
    // Session clock failure also happens after ticket consumption.
    // ------------------------------------------------------------------
    StorageHandoffConsumeService
        bad_clock_service(
            make_config(
                base_key.data(),
                &policy,
                []() -> long {
                    return 0;
                },
                600
            )
        );

    ticket =
        issue_ticket(
            local_node,
            17,
            "clock-failure ticket"
        );

    expect_result(
        bad_clock_service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            ClockUnavailable,
        "session clock failure fails closed"
    );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "clock failure does not restore ticket"
    );


    // ------------------------------------------------------------------
    // Static configuration errors are detected before ticket consumption.
    // ------------------------------------------------------------------
    StorageHandoffConsumeService
        bad_ttl_service(
            make_config(
                base_key.data(),
                &policy,
                [&]() -> long {
                    return session_now;
                },
                pqnas::cluster::
                    kStorageUserSessionCookieMaxTtlSeconds +
                    1
            )
        );

    ticket =
        issue_ticket(
            local_node,
            17,
            "bad-config ticket"
        );

    expect_result(
        bad_ttl_service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            InvalidConfiguration,
        "invalid static TTL configuration rejected before consume"
    );

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &output
        ),
        StorageHandoffConsumeServiceResult::
            Ok,
        "static configuration failure did not burn ticket"
    );


    // ------------------------------------------------------------------
    // Concurrent redemption: exactly one winner.
    // ------------------------------------------------------------------
    current_projection =
        valid_projection(
            fingerprint
        );

    ticket =
        issue_ticket(
            local_node,
            17,
            "concurrent ticket"
        );

    StorageHandoffConsumeServiceOutput
        concurrent_one;

    StorageHandoffConsumeServiceOutput
        concurrent_two;

    StorageHandoffConsumeServiceResult
        result_one =
            StorageHandoffConsumeServiceResult::
                InternalError;

    StorageHandoffConsumeServiceResult
        result_two =
            StorageHandoffConsumeServiceResult::
                InternalError;

    std::thread first(
        [&]() {
            result_one =
                service.consume(
                    ticket,
                    browser_binding_secret,
                    &concurrent_one
                );
        }
    );

    std::thread second(
        [&]() {
            result_two =
                service.consume(
                    ticket,
                    browser_binding_secret,
                    &concurrent_two
                );
        }
    );

    first.join();
    second.join();

    const int success_count =
        (result_one ==
            StorageHandoffConsumeServiceResult::Ok
            ? 1
            : 0) +
        (result_two ==
            StorageHandoffConsumeServiceResult::Ok
            ? 1
            : 0);

    const int rejected_count =
        (result_one ==
            StorageHandoffConsumeServiceResult::Rejected
            ? 1
            : 0) +
        (result_two ==
            StorageHandoffConsumeServiceResult::Rejected
            ? 1
            : 0);

    expect(
        success_count == 1 &&
        rejected_count == 1,
        "concurrent ticket has exactly one successful consumer"
    );

    expect(
        concurrent_one.session_cookie_value.empty() !=
            concurrent_two.session_cookie_value.empty(),
        "only concurrent winner receives a session cookie"
    );

    StorageHandoffConsumeServiceOutput
        replay_after_race;

    expect_result(
        service.consume(
            ticket,
            browser_binding_secret,
            &replay_after_race
        ),
        StorageHandoffConsumeServiceResult::
            Rejected,
        "concurrent ticket remains spent after race"
    );


    pqnas::cluster::
        clear_storage_handoff_browser_binding_secret_text(
            &browser_binding_secret
        );

    pqnas::cluster::
        clear_storage_handoff_browser_binding_secret_text(
            &wrong_browser_binding_secret
        );

    if (failures != 0) {
        std::cerr
            << failures
            << " Storage handoff consume service test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: Storage handoff consume service tests passed\n";

    return 0;
}
