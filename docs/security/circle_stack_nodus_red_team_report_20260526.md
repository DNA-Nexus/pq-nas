# Circle Stack Nodus Federation Red-Team Report

## Executive Summary

The Circle Stack Nodus federation feature implements a DHT-based public post
distribution system. The overall design is sound for a **public, best-effort**
federation model, and the code demonstrates good defensive habits (shell quoting,
path traversal protection, SQLite parameterized queries, admin-only gating on
debug routes).

However, the most significant gap is the **complete absence of cryptographic
event signing**. Any Nodus DHT participant can forge events claiming any
`origin_nas`, enabling impersonation of any NAS in the federation. This is the
primary finding and should be addressed before this feature is used in a
trust-sensitive context.

Secondary concerns include unbounded memory consumption from malicious Nodus
responses, lack of ingestion rate limiting in the inbox worker, and stale preview
cache files surviving post visibility changes.

**Biggest risks (ordered):**
1. Event forgery / origin impersonation (no signatures)
2. Resource exhaustion via DHT poisoning (unbounded ingest)
3. Unbounded nodus-cli subprocess output (OOM potential)

## Scope

**Branch:** `feature/circle-stack-nodus-federation`

**Files reviewed:**
- `server/src/federation/circle_federation_event.{h,cpp}`
- `server/src/federation/circle_federation_outbox.{h,cpp}`
- `server/src/federation/circle_federation_inbox.{h,cpp}`
- `server/src/federation/circle_federation_remote_feed.{h,cpp}`
- `server/src/federation/circle_federation_outbox_worker.{h,cpp}`
- `server/src/federation/pqnas_nodus_client.{h,cpp}`
- `server/src/routes_circle_nodus_research.{h,cpp}`
- `server/src/circle_stack_routes.cpp` (federation + media-preview sections)
- `server/src/static/admin_settings.{html,js}`
- `server/src/storage_resolver.{h,cpp}` (path normalization)
- `tools/installer/pqnas_install.py`
- `tools/release/make_tarball.sh`

**Assumptions:**
- Nodus is a Kademlia-style DHT where any authenticated node can PUT values at
  arbitrary keys. Values are not intrinsically authenticated by Nodus itself
  (i.e., Nodus does not enforce who can write which key).
- The PQ-NAS server runs as the `pqnas` service user.
- `PQNAS_PUBLIC_BASE_URL` is set by the operator during install.

---

## Findings

### Finding 1: Federation events are unsigned — origin impersonation is trivial

**Severity:** High
**Confidence:** High
**Status:** Does not block merge for a public-beta/research deployment, but blocks any trust-dependent use of federated content.

**Affected code:**
- `server/src/circle_stack_routes.cpp:1711-1733` — event JSON construction
- `server/src/federation/circle_federation_outbox_worker.cpp:368-411` — `worker_pull_latest_remote_head` ingestion
- `server/src/federation/circle_federation_outbox_worker.cpp:416-485` — `worker_fetch_event_to_inbox` ingestion

**What is wrong:**
Federation events published to Nodus are plain JSON with no cryptographic
signature. The `origin_nas` field (the Nodus fingerprint of the publishing NAS)
is a self-asserted string. The receiving worker validates structural consistency
(circle_id/event_id match the key, fields are non-empty, origin is not self) but
never verifies that the event was actually published by the claimed origin.

Since Nodus is a DHT where any node can write to any key, an attacker with a
Nodus identity can publish crafted events under the standard key namespace
(`pqnas:circlestack:circle:local-public-feed:event:{event_id}`) claiming any
`origin_nas` fingerprint.

**Exploit scenario:**
1. Attacker creates a Nodus identity with `nodus-cli identity-init`.
2. Attacker constructs a JSON event with `origin_nas` set to a victim NAS's
   fingerprint (obtained from any previously observed federation event).
3. Attacker publishes the forged event to the DHT:
   `nodus-cli put pqnas:circlestack:circle:local-public-feed:event:fake_evt_1 '{...}'`
4. Attacker updates the head pointer:
   `nodus-cli put pqnas:circlestack:circle:local-public-feed:head fake_evt_1`
5. Attacker adds the event to recent:index.
6. All subscribing NAS instances ingest the forged event and display it in their
   federated feed as originating from the victim NAS.

**Impact:**
- Impersonation of any NAS / user identity in federated feeds.
- Phishing content displayed under a trusted origin identity.
- Defamation or social engineering via forged posts.

**Recommended fix:**
Sign the event JSON with the publishing NAS's Nodus private key (Ed25519 or
Kyber, whichever Nodus supports). Include the signature in the event envelope.
The receiving worker should verify the signature against the claimed `origin_nas`
public key before storing the event.

Minimum viable approach: include a detached Ed25519 signature field in the event
JSON:
```json
{
  "origin_nas": "<fingerprint>",
  "origin_sig": "<base64 Ed25519 sig over canonical event body>",
  ...
}
```

**Verification test:**
Use a separate Nodus identity to publish a forged event claiming a known NAS
fingerprint. Verify that the receiving NAS rejects it after signing is
implemented.

---

### Finding 2: Unbounded output from nodus-cli subprocess — OOM risk

**Severity:** Medium
**Confidence:** High
**Status:** Does not block merge but should be fixed before production.

**Affected code:**
- `server/src/federation/pqnas_nodus_client.cpp:13-54` — `run_command_capture`

**What is wrong:**
The `run_command_capture` function reads all output from `nodus-cli` into a
`std::string` with no size limit:

```cpp
while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
    result.output.append(buffer.data());
}
```

If a Nodus seed returns a very large value for a key (the DHT may allow
multi-megabyte values), the output string grows without bound. The `timeout`
command limits execution time (default 8 seconds) but not output volume. A
malicious value on the DHT could cause the worker to allocate hundreds of MB.

Note: the admin route equivalent (`run_shell_capture_limited` in
`routes_circle_nodus_research.cpp:188`) correctly caps output at 4096 bytes.
The worker code path does not.

**Exploit scenario:**
1. Attacker publishes a multi-megabyte value at a standard key on Nodus.
2. Worker performs `nodus-cli get` and reads the entire value into memory.
3. Repeated across multiple keys or seeds, this could exhaust server RAM.

**Impact:**
- Worker OOM leading to PQ-NAS service crash.
- Potential system instability on memory-constrained NAS devices.

**Recommended fix:**
Add a size cap to `run_command_capture`, similar to `run_shell_capture_limited`:

```cpp
constexpr std::size_t kMaxOutput = 65536; // 64 KB
while (fgets(...)) {
    result.output.append(buffer.data());
    if (result.output.size() >= kMaxOutput) {
        result.output.resize(kMaxOutput);
        result.output += "\n...[truncated]";
        break;
    }
}
```

**Verification test:**
Publish a 10 MB value to a Nodus key. Observe that the worker truncates output
and does not allocate excessive memory.

---

### Finding 3: No ingestion rate limiting or inbox size cap — storage exhaustion via DHT flooding

**Severity:** Medium
**Confidence:** High
**Status:** Does not block merge for limited beta, should be fixed before wider deployment.

**Affected code:**
- `server/src/federation/circle_federation_outbox_worker.cpp:487-543` — `worker_pull_recent_index_remote_events`
- `server/src/federation/circle_federation_inbox.cpp:133-186` — `store_circle_federation_inbox_event`
- `server/src/federation/circle_federation_remote_feed.cpp:137-198` — `store_circle_federation_remote_feed_event`

**What is wrong:**
The worker ingests events from Nodus into the inbox SQLite database using
`INSERT OR IGNORE`. While duplicates are prevented by the unique event_id index,
there is:
- No maximum inbox/remote_feed table size.
- No pruning or eviction of old entries.
- No rate limit on how many events are ingested per cycle.

The `max_items` clamp in `worker_pull_recent_index_remote_events` limits to 100
items per cycle, and the recent:index itself is clamped to 100, but over time
(or with a poisoned index that changes each cycle), thousands of events
accumulate.

**Exploit scenario:**
1. Attacker publishes thousands of unique fake events to Nodus under the target
   circle_id's key namespace.
2. Attacker updates recent:index each worker cycle with new event IDs.
3. The victim NAS ingests 100 events per 10-second cycle.
4. Over hours, the inbox and remote_feed SQLite databases grow to fill available
   disk space.

**Impact:**
- Disk exhaustion on the NAS device.
- SQLite performance degradation as tables grow unbounded.
- Potential service disruption.

**Recommended fix:**
- Add a configurable maximum row count for inbox and remote_feed tables (e.g.,
  10,000 events).
- Implement periodic pruning: delete oldest entries when the cap is exceeded.
- Consider a per-origin_nas rate limit or cap.

**Verification test:**
Set a small cap (e.g., 50 events) and verify that old events are pruned when new
ones arrive.

---

### Finding 4: Federation event JSON stored without size validation

**Severity:** Medium
**Confidence:** Medium
**Status:** Does not block merge.

**Affected code:**
- `server/src/federation/circle_federation_inbox.cpp:133-186` — `store_circle_federation_inbox_event`
- `server/src/federation/circle_federation_remote_feed.cpp:137-198` — `store_circle_federation_remote_feed_event`
- `server/src/federation/circle_federation_outbox_worker.cpp:368-411` — event JSON parsing

**What is wrong:**
The `event_json` field stored in both the inbox and remote_feed tables has no
maximum size check. A malicious event with a very large JSON payload (e.g.,
multi-megabyte `text_preview` or deeply nested payload) will be parsed by
`nlohmann::json::parse()` (allocating proportional memory) and then stored in
full in SQLite.

The outbox side correctly uses `clamp_error` (2048 byte cap) for error strings
but applies no similar cap to event JSON.

**Exploit scenario:**
1. Attacker creates an event with a 5 MB `text_preview` field.
2. Worker parses it (5 MB allocation) and stores it in the inbox table.
3. Remote feed also stores the full 5 MB JSON.
4. Repeated ingestion of oversized events fills disk and degrades SQLite
   performance.

**Impact:**
- Memory spikes during JSON parsing.
- Database bloat.
- Compounds with Finding 3 for faster disk exhaustion.

**Recommended fix:**
Before parsing, check that the raw event JSON size does not exceed a reasonable
limit (e.g., 32 KB or 64 KB). Reject oversized events with an error.

```cpp
if (event_json_raw.size() > 65536) {
    // reject: event too large
}
```

**Verification test:**
Publish an oversized event (>64 KB) and verify it is rejected before parsing.

---

### Finding 5: Stale preview cache files survive post visibility changes

**Severity:** Low
**Confidence:** High
**Status:** Does not block merge.

**Affected code:**
- `server/src/circle_stack_routes.cpp:2504-2581` — `cs_generate_federation_preview_jpeg`
- `server/src/circle_stack_routes.cpp:2525-2526` — cache directory

**What is wrong:**
When a post's visibility changes from "public" to "private" or "circle", the
media-preview endpoint correctly refuses to serve the preview (the visibility
check in `cs_resolve_public_federation_media_ref_source` happens before the
cache lookup in `cs_generate_federation_preview_jpeg`). However, the previously
generated preview JPEG remains on disk at
`/srv/pqnas/cache/circlestack/federation-previews/{hash}.jpg`.

There is no cache eviction or invalidation mechanism.

**Exploit scenario:**
This is not directly exploitable — the cached file is not served through any
HTTP route after the post becomes non-public. However:
1. An attacker with filesystem access to the NAS could find and view cached
   previews of previously-public media.
2. The cache grows without bound over time.

**Impact:**
- Minor data retention concern: preview images of previously-public posts
  persist on disk indefinitely.
- Cache disk usage grows without bound.

**Recommended fix:**
- Implement periodic cache cleanup (e.g., delete files older than 7 days, or
  limit total cache size).
- Consider clearing a post's cached preview when its visibility changes.

**Verification test:**
Create a public post with an image, trigger preview generation, change the post
to "circle" visibility, then verify that the cached JPEG is eventually removed.

---

### Finding 6: Nodus CLI global mutex serializes all DHT operations — DoS amplification

**Severity:** Low
**Confidence:** High
**Status:** Does not block merge. Known design limitation per code comment.

**Affected code:**
- `server/src/federation/pqnas_nodus_client.cpp:13-20` — `nodus_cli_mutex`

**What is wrong:**
All `nodus_cli_put` and `nodus_cli_get` calls are serialized through a single
global mutex. The code comment explicitly acknowledges this as a temporary
research adapter. With a default 8-second timeout per call, and the worker
iterating over multiple seeds (up to 7) with multiple operations per event (put
event, put head, put recent, put recent:index = 4 ops per seed), a single
unresponsive seed can block all Nodus operations for up to 8 seconds per call.

Additionally, the admin routes (nodus/status, put-test, get-test, etc.) share
the same mutex via `nodus_cli_put`/`nodus_cli_get`. An admin status check
during a worker cycle will block until the worker's current CLI call completes.

**Exploit scenario:**
1. One of the hardcoded seed IPs becomes unreachable (network issue or DoS).
2. Every CLI call to that seed hangs for 8 seconds before timing out.
3. Worker cycle with 7 seeds × 4+ operations = potentially 28+ sequential 8s
   timeouts = ~224 seconds of blocking.
4. Admin panel Nodus status requests queue behind the worker and appear hung.

**Impact:**
- Worker throughput degradation when any seed is slow/unreachable.
- Admin panel unresponsiveness during worker activity.

**Recommended fix:**
- Short term: reduce default timeout to 3-5 seconds. Skip seeds that
  consistently fail (circuit breaker pattern).
- Long term: replace popen-based CLI calls with a persistent Nodus client
  connection or async subprocess management, as noted in the code comment.

**Verification test:**
Block one seed IP with iptables, verify the worker cycle completes in a
reasonable time and doesn't block admin routes.

---

### Finding 7: Public federation events expose NAS public URL and full user fingerprints

**Severity:** Low
**Confidence:** High
**Status:** Does not block merge. By-design for public federation.

**Affected code:**
- `server/src/circle_stack_routes.cpp:1645-1657` — `cs_make_federation_origin_descriptor`
- `server/src/circle_stack_routes.cpp:1711-1733` — event JSON with `owner_fp`, `preview_base_url`

**What is wrong:**
Every public federation event published to Nodus contains:
- `origin_nas`: the full Nodus fingerprint of the publishing NAS.
- `origin.preview_base_url`: the NAS's public URL (from `PQNAS_PUBLIC_BASE_URL`).
- `payload.owner_fp`: the full user fingerprint of the post author.
- `payload.owner_display_name`: the user's display name.

These are readable by any Nodus participant.

**Exploit scenario:**
1. Attacker monitors Nodus for events in the `local-public-feed` circle.
2. Attacker collects all participating NAS public URLs and user fingerprints.
3. Attacker uses public URLs for reconnaissance (port scanning, version
   fingerprinting) or targeted attacks.
4. Attacker correlates user fingerprints across NAS instances to build user
   activity profiles.

**Impact:**
- NAS server discovery and targeting via `preview_base_url`.
- User identity enumeration and activity correlation.
- Metadata useful for social engineering.

**Recommended fix:**
- Consider whether `preview_base_url` needs to be in every event, or whether
  it could be published as a separate NAS presence record that is looked up
  on demand.
- Consider truncating `owner_fp` to a short prefix in events (the
  `owner_fp_short` field already exists but `owner_fp` is also included).
- Document these privacy trade-offs for operators.

**Verification test:**
Review events on Nodus DHT and verify that the exposure matches the operator's
expectations for a public federation.

---

### Finding 8: Media-preview endpoint has no rate limiting — ffmpeg CPU exhaustion

**Severity:** Low
**Confidence:** Medium (depends on deployment: reverse proxy may mitigate)
**Status:** Does not block merge.

**Affected code:**
- `server/src/circle_stack_routes.cpp:2827-2901` — media-preview GET handler
- `server/src/circle_stack_routes.cpp:2553-2560` — ffmpeg subprocess invocation

**What is wrong:**
The `/api/v4/circlestack/federation/media-preview` endpoint is unauthenticated
(by design, for federation). On a cache miss, it invokes ffmpeg as a subprocess
to generate a preview JPEG. The ffmpeg call has a 20-second timeout but there is
no rate limiting on the endpoint.

An attacker who knows valid `event_id` + `ref_id` pairs (obtainable from public
Nodus events) can trigger concurrent ffmpeg invocations.

**Exploit scenario:**
1. Attacker reads federation events from Nodus to collect valid event_id/ref_id
   pairs for posts with media.
2. Attacker sends many concurrent requests to the media-preview endpoint with
   different valid pairs.
3. Each request spawns an ffmpeg process (up to 20s each).
4. NAS CPU is saturated by parallel ffmpeg processes.

**Impact:**
- CPU exhaustion on the NAS server.
- Degraded performance for all NAS services.

**Recommended fix:**
- Limit concurrent ffmpeg preview generation (e.g., semaphore allowing max 2
  concurrent conversions).
- Add general rate limiting on the media-preview endpoint (e.g., via reverse
  proxy or application-level throttle).
- Pre-generate previews at post creation time instead of on-demand.

**Verification test:**
Send 20 concurrent media-preview requests for uncached previews. Verify that
concurrency is bounded and the server remains responsive.

---

## Non-findings / Checked Areas

### Shell command injection — NOT FOUND
All user/DHT-derived values passed to `popen` or `std::system` are properly
shell-quoted using `shell_quote_for_nodus_research` (single-quote wrapping with
correct escaping of embedded quotes). The `timeout` seconds values are integers
from `std::clamp`. Checked in:
- `pqnas_nodus_client.cpp:56-81` — `build_base_command`
- `pqnas_nodus_client.cpp:114-149` — `nodus_cli_put` / `nodus_cli_get`
- `circle_stack_routes.cpp:2553-2558` — ffmpeg command
- `routes_circle_nodus_research.cpp:755-760` — identity-init command

### Path traversal in media-preview — NOT FOUND
The media-preview endpoint correctly:
1. Validates ref_id format strictly (`cs_parse_federation_media_ref_id`): only
   `{post|reply}:{integer}:media:primary` accepted.
2. Looks up media_path from the database (not from user input).
3. Normalizes the path (`normalize_user_rel_path_strict`): rejects `.`, `..`,
   absolute paths, NUL bytes, and the `.pqnas` reserved namespace.
4. Checks for symlinks in every path component
   (`cs_path_has_no_symlink_components_below_root`).
5. Verifies post visibility is "public".
6. Validates event_id matches the expected format for the specific post/reply.

### SQL injection — NOT FOUND
All SQLite operations use parameterized queries (`sqlite3_bind_text`,
`sqlite3_bind_int64`). No string interpolation into SQL. Checked across all
four federation SQLite databases (outbox, inbox, remote_feed, CircleStack
main DB).

### Admin route access control — CORRECT
All `/api/v4/admin/nodus/*` routes use `require_admin()` which:
1. Validates cookie-based authentication.
2. Checks `actor_role == "admin"`.
3. Returns 403 for non-admin users.
No admin-only data is exposed through non-admin routes.

### Admin routes exposing secrets — NOT FOUND
Admin Nodus status returns config paths and the NAS fingerprint (appropriate for
admin use). It does not return private keys, cookie keys, or other secrets. The
`outbox_event_json` and `inbox_event_json` functions deliberately exclude the
full `event_json` from list responses, limiting information exposure.

### Nodus key namespace restriction — CORRECT
The admin put-test and get-test routes enforce `valid_research_key()` which
requires keys to start with `pqnas:` and be at most 512 bytes. This prevents
admin users from reading/writing arbitrary Nodus keys outside the PQ-NAS
namespace.

### Private post leakage through federation — NOT FOUND
`cs_enqueue_post_created_federation_best_effort` (line 1681) explicitly checks
`if (visibility != "public") return false`. Non-public posts are never enqueued
for federation. The same pattern applies to replies and reactions — they are only
federated when the parent post is public.

### Installer Nodus identity permissions — CORRECT
The `chmod_nodus_identity_tree` function sets:
- Identity directory: `0o700`
- All key files (`nodus.pk`, `nodus.sk`, `nodus.fp`, `nodus.kyber_pk`,
  `nodus.kyber_sk`): `0o600`
- Ownership: `pqnas:pqnas`

Private key material is not readable by other users.

### Outbox lease recovery — CORRECT
The `claim_circle_federation_outbox_pending` function uses `BEGIN IMMEDIATE`
transactions and recovers stale publishing leases (where `next_attempt_epoch`
has expired). The retry mechanism uses exponential backoff (30s → 60s → ... →
3600s) with a configurable max attempts (default 5). Failed events are marked
permanently failed, not retried indefinitely.

### JSON escape correctness — CORRECT
The `json_escape` function in `circle_federation_event.cpp` correctly handles
all JSON special characters and control characters (< 0x20) with `\u00xx`
escapes. For the bulk of event JSON, `nlohmann::json` library handles
serialization, which is well-tested.

### Event deduplication — CORRECT
Both inbox and remote_feed tables have `UNIQUE INDEX` on `event_id`. Insert
operations use `INSERT OR IGNORE`, so duplicate events are silently discarded.
This provides basic replay protection at the event_id level.

### Self-event filtering — CORRECT
The worker checks `origin_nas == local_nodus_fp` and skips events that
originated from the local NAS (lines 392, 464, 608, 649). This prevents
echo loops.

---

## Recommended Fix Order

1. **Finding 2: Cap nodus-cli output size** — Simple fix, prevents OOM.
   Add a size limit to `run_command_capture`. Low effort, high value.

2. **Finding 4: Validate event JSON size before parsing** — Simple check
   before `json::parse()`. Low effort, prevents memory spikes.

3. **Finding 3: Add inbox/remote_feed table size caps and pruning** — Medium
   effort. Prevents storage exhaustion from DHT flooding.

4. **Finding 1: Implement event signing** — Highest severity but highest
   effort. Requires deciding on a signature scheme, modifying event format,
   and implementing verification in the worker. This is the fundamental
   trust gap in the federation protocol.

5. **Finding 8: Limit concurrent ffmpeg invocations** — Add a semaphore.
   Low effort.

6. **Finding 5: Implement preview cache eviction** — Low priority.
   Periodic cleanup job.

7. **Finding 6: Nodus CLI mutex / timeout optimization** — Medium effort.
   Circuit breaker or async CLI calls.

8. **Finding 7: Privacy documentation** — Document the metadata exposure
   trade-offs for operators enabling federation.

---

## Questions / Unknowns

1. **Nodus DHT write permissions**: This review assumes any Nodus node can
   write to any key. If Nodus has key-ownership enforcement (e.g., only the
   key creator can update it), Finding 1 severity would be reduced. The source
   code for Nodus itself was not available for review.

2. **Nodus value size limits**: If the Nodus DHT enforces a maximum value size
   (e.g., 64 KB), Findings 2 and 4 severity would be reduced. This depends on
   the Nodus protocol spec.

3. **Reverse proxy rate limiting**: If deployments consistently use nginx with
   rate limiting in front of PQ-NAS, Finding 8 is partially mitigated at the
   infrastructure level. The installer supports optional nginx setup, but rate
   limiting configuration was not reviewed.

4. **nodus-cli output format**: The `extract_nodus_value` function parses
   nodus-cli output by looking for `"Value: "` prefix. If nodus-cli output
   format changes, this parsing could silently fail. The nodus-cli interface
   contract was not available for review.

5. **Circle ID expansion**: Currently, all public federation uses
   `circle_id = "local-public-feed"` (hardcoded). If this expands to
   user-created circles, additional namespace isolation and access control
   considerations would arise.
