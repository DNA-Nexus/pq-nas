#include "cluster/storage_handoff_ticket_store.h"

#include <sodium.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>

namespace {

using pqnas::cluster::
    StorageHandoffTicketClaims;

using pqnas::cluster::
    StorageHandoffTicketResult;

using pqnas::cluster::
    StorageHandoffTicketStore;

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
    StorageHandoffTicketResult actual,
    StorageHandoffTicketResult expected,
    const std::string& message
) {
    expect(
        actual == expected,
        message +
            ": expected " +
            pqnas::cluster::
                storage_handoff_ticket_result_code(
                    expected
                ) +
            ", got " +
            pqnas::cluster::
                storage_handoff_ticket_result_code(
                    actual
                )
    );
}

bool base64url_text(
    const std::string& value
) {
    for (unsigned char ch : value) {
        const bool alphanumeric =
            (ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9');

        if (!alphanumeric &&
            ch != '-' &&
            ch != '_') {
            return false;
        }
    }

    return true;
}

} // namespace

int main() {
    if (sodium_init() < 0) {
        std::cerr
            << "FAIL: sodium initialization failed\n";
        return 1;
    }

    const std::string fingerprint(
        pqnas::cluster::
            kStorageHandoffFingerprintHexChars,
        'a'
    );

    const std::string home_node =
        "storage-01";

    constexpr std::uint64_t
        projection_revision = 17;

    // Deterministic canonical 32-byte base64url fixture.
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
        "hash deterministic browser binding fixture"
    );

    std::string generated_binding_secret;

    expect(
        pqnas::cluster::
            generate_storage_handoff_browser_binding_secret(
                &generated_binding_secret
            ) &&
        generated_binding_secret.size() ==
            pqnas::cluster::
                kStorageHandoffBrowserBindingChars,
        "generate production-shaped browser binding secret"
    );

    pqnas::cluster::
        clear_storage_handoff_browser_binding_secret_text(
            &generated_binding_secret
        );

    std::int64_t now = 1000;

    StorageHandoffTicketStore store(
        4,
        [&now]() {
            return now;
        }
    );

    {
        std::string invalid_output =
            "stale";

        expect_result(
            store.issue(
                std::string(
                    pqnas::cluster::
                        kStorageHandoffFingerprintHexChars,
                    'A'
                ),
                home_node,
                projection_revision,
                browser_binding_hash,
                30,
                &invalid_output,
                nullptr
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "uppercase non-canonical fingerprint rejected"
        );

        expect(
            invalid_output.empty(),
            "uppercase fingerprint failure clears ticket output"
        );

        invalid_output =
            "stale";

        expect_result(
            store.issue(
                fingerprint,
                home_node,
                projection_revision,
                std::string(
                    pqnas::cluster::
                        kStorageHandoffBrowserBindingChars,
                    'B'
                ),
                30,
                &invalid_output,
                nullptr
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "non-canonical browser binding hash rejected"
        );

        expect(
            invalid_output.empty(),
            "invalid browser binding hash clears ticket output"
        );
    }

    std::string ticket = "stale";
    std::int64_t expires_at = -1;

    expect_result(
        store.issue(
            fingerprint,
            home_node,
            projection_revision,
            browser_binding_hash,
            30,
            &ticket,
            &expires_at
        ),
        StorageHandoffTicketResult::Ok,
        "issue one-time handoff ticket"
    );

    expect(
        ticket.size() ==
            pqnas::cluster::
                kStorageHandoffTicketChars &&
        base64url_text(ticket),
        "issued ticket format"
    );

    expect(
        expires_at == 1030,
        "issued ticket expiry"
    );

    expect(
        store.size() == 1,
        "issued ticket stored"
    );

    {
        StorageHandoffTicketClaims
            wrong_binding_claims;

        expect_result(
            store.consume(
                ticket,
                wrong_browser_binding_secret,
                &wrong_binding_claims
            ),
            StorageHandoffTicketResult::
                BindingMismatch,
            "wrong browser binding rejects existing ticket"
        );

        expect(
            store.size() == 1,
            "wrong browser binding does not burn ticket"
        );

        expect(
            wrong_binding_claims.fingerprint.empty() &&
            wrong_binding_claims.home_node.empty() &&
            wrong_binding_claims.projection_revision == 0,
            "wrong browser binding returns no ticket claims"
        );
    }

    now = 1001;

    StorageHandoffTicketClaims claims;

    expect_result(
        store.consume(
            ticket,
            browser_binding_secret,
            &claims
        ),
        StorageHandoffTicketResult::Ok,
        "consume ticket once"
    );

    expect(
        claims.fingerprint == fingerprint &&
        claims.home_node == home_node &&
        claims.projection_revision ==
            projection_revision &&
        claims.issued_at == 1000 &&
        claims.expires_at == 1030,
        "consumed ticket claims"
    );

    expect(
        store.size() == 0,
        "successful consume erases ticket"
    );

    expect_result(
        store.consume(
            ticket,
            browser_binding_secret,
            &claims
        ),
        StorageHandoffTicketResult::NotFound,
        "consumed ticket cannot replay"
    );

    pqnas::cluster::
        clear_storage_handoff_ticket_text(
            &ticket
        );

    expect(
        ticket.empty(),
        "ticket clear helper"
    );

    {
        std::string bad_ticket(43, '!');
        StorageHandoffTicketClaims bad_claims;

        expect_result(
            store.consume(
                bad_ticket,
                browser_binding_secret,
                &bad_claims
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "malformed ticket rejected"
        );
    }

    {
        now = 2000;

        std::string expiring_ticket;

        expect_result(
            store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                5,
                &expiring_ticket,
                nullptr
            ),
            StorageHandoffTicketResult::Ok,
            "issue expiry fixture"
        );

        now = 2005;

        expect_result(
            store.consume(
                expiring_ticket,
                browser_binding_secret,
                &claims
            ),
            StorageHandoffTicketResult::Expired,
            "ticket expires at exact boundary"
        );

        expect(
            store.size() == 0,
            "expired consume erases ticket"
        );

        pqnas::cluster::
            clear_storage_handoff_ticket_text(
                &expiring_ticket
            );
    }

    {
        now = 3000;

        std::string first;
        std::string second;

        expect_result(
            store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                5,
                &first,
                nullptr
            ),
            StorageHandoffTicketResult::Ok,
            "issue first prune fixture"
        );

        now = 3001;

        expect_result(
            store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                20,
                &second,
                nullptr
            ),
            StorageHandoffTicketResult::Ok,
            "issue second prune fixture"
        );

        expect(
            store.size() == 2,
            "prune fixtures present"
        );

        now = 3005;
        store.prune_expired();

        expect(
            store.size() == 1,
            "prune removes only expired ticket"
        );

        pqnas::cluster::
            clear_storage_handoff_ticket_text(
                &first
            );

        pqnas::cluster::
            clear_storage_handoff_ticket_text(
                &second
            );
    }

    {
        now = 4000;

        StorageHandoffTicketStore small_store(
            2,
            [&now]() {
                return now;
            }
        );

        std::string first;
        std::string second;
        std::string third = "stale";

        expect_result(
            small_store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                1,
                &first,
                nullptr
            ),
            StorageHandoffTicketResult::Ok,
            "fill bounded store first ticket"
        );

        expect_result(
            small_store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                1,
                &second,
                nullptr
            ),
            StorageHandoffTicketResult::Ok,
            "fill bounded store second ticket"
        );

        expect_result(
            small_store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                1,
                &third,
                nullptr
            ),
            StorageHandoffTicketResult::StoreFull,
            "bounded store refuses overflow"
        );

        expect(
            third.empty(),
            "failed issue clears plaintext output"
        );

        now = 4001;

        expect_result(
            small_store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                1,
                &third,
                nullptr
            ),
            StorageHandoffTicketResult::Ok,
            "issue prunes expired entries before capacity check"
        );

        expect(
            small_store.size() == 1,
            "capacity recovery after expiry"
        );

        pqnas::cluster::
            clear_storage_handoff_ticket_text(
                &first
            );

        pqnas::cluster::
            clear_storage_handoff_ticket_text(
                &second
            );

        pqnas::cluster::
            clear_storage_handoff_ticket_text(
                &third
            );
    }

    {
        std::string output = "stale";

        expect_result(
            store.issue(
                std::string(127, 'a'),
                home_node,
                projection_revision,
                browser_binding_hash,
                30,
                &output,
                nullptr
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "short fingerprint rejected"
        );

        expect(
            output.empty(),
            "invalid issue clears output"
        );

        expect_result(
            store.issue(
                fingerprint,
                "../storage-01",
                projection_revision,
                browser_binding_hash,
                30,
                &output,
                nullptr
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "invalid home node rejected"
        );

        expect(
            output.empty(),
            "invalid home node clears output"
        );

        expect_result(
            store.issue(
                fingerprint,
                home_node,
                0,
                browser_binding_hash,
                30,
                &output,
                nullptr
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "zero projection revision rejected"
        );

        expect(
            output.empty(),
            "zero projection revision clears output"
        );

        expect_result(
            store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                0,
                &output,
                nullptr
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "zero TTL rejected"
        );

        expect_result(
            store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                pqnas::cluster::
                    kStorageHandoffTicketMaxTtlSeconds +
                    1,
                &output,
                nullptr
            ),
            StorageHandoffTicketResult::
                InvalidInput,
            "oversized TTL rejected"
        );
    }

    {
        now = 5000;

        StorageHandoffTicketStore concurrent_store(
            4,
            [&now]() {
                return now;
            }
        );

        std::string concurrent_ticket;

        expect_result(
            concurrent_store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                30,
                &concurrent_ticket,
                nullptr
            ),
            StorageHandoffTicketResult::Ok,
            "issue concurrent consume fixture"
        );

        StorageHandoffTicketClaims claims_one;
        StorageHandoffTicketClaims claims_two;

        StorageHandoffTicketResult result_one =
            StorageHandoffTicketResult::InternalError;

        StorageHandoffTicketResult result_two =
            StorageHandoffTicketResult::InternalError;

        std::atomic<bool> start{false};

        std::thread first_consumer(
            [&]() {
                while (!start.load(
                    std::memory_order_acquire
                )) {
                    std::this_thread::yield();
                }

                result_one =
                    concurrent_store.consume(
                        concurrent_ticket,
                        browser_binding_secret,
                        &claims_one
                    );
            }
        );

        std::thread second_consumer(
            [&]() {
                while (!start.load(
                    std::memory_order_acquire
                )) {
                    std::this_thread::yield();
                }

                result_two =
                    concurrent_store.consume(
                        concurrent_ticket,
                        browser_binding_secret,
                        &claims_two
                    );
            }
        );

        start.store(
            true,
            std::memory_order_release
        );

        first_consumer.join();
        second_consumer.join();

        const int ok_count =
            (result_one ==
                StorageHandoffTicketResult::Ok ? 1 : 0) +
            (result_two ==
                StorageHandoffTicketResult::Ok ? 1 : 0);

        const int not_found_count =
            (result_one ==
                StorageHandoffTicketResult::NotFound ? 1 : 0) +
            (result_two ==
                StorageHandoffTicketResult::NotFound ? 1 : 0);

        expect(
            ok_count == 1 &&
            not_found_count == 1,
            "simultaneous consume must allow exactly one redemption"
        );

        const StorageHandoffTicketClaims&
            successful_claims =
                result_one ==
                    StorageHandoffTicketResult::Ok
                    ? claims_one
                    : claims_two;

        expect(
            successful_claims.fingerprint ==
                fingerprint &&
            successful_claims.home_node ==
                home_node &&
            successful_claims.projection_revision ==
                projection_revision,
            "successful concurrent consume returns bound claims"
        );

        expect(
            concurrent_store.size() == 0,
            "concurrent consume removes ticket"
        );

        pqnas::cluster::
            clear_storage_handoff_ticket_text(
                &concurrent_ticket
            );
    }

    {
        StorageHandoffTicketStore bad_clock_store(
            4,
            []() -> std::int64_t {
                return 0;
            }
        );

        std::string output;

        expect_result(
            bad_clock_store.issue(
                fingerprint,
                home_node,
                projection_revision,
                browser_binding_hash,
                30,
                &output,
                nullptr
            ),
            StorageHandoffTicketResult::
                ClockUnavailable,
            "invalid clock fails closed"
        );
    }

    expect_result(
        store.issue(
            fingerprint,
            home_node,
            projection_revision,
            browser_binding_hash,
            30,
            nullptr,
            nullptr
        ),
        StorageHandoffTicketResult::
            InvalidInput,
        "missing ticket output rejected"
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
            << " storage handoff ticket test(s) failed\n";

        return 1;
    }

    std::cout
        << "OK: storage handoff ticket tests passed\n";

    return 0;
}
