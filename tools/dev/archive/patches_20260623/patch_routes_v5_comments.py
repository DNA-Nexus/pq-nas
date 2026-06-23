#!/usr/bin/env python3
from pathlib import Path
import sys

p = Path("server/src/routes_v5.cc")
if not p.exists():
    print(f"ERROR: missing {p}", file=sys.stderr)
    sys.exit(1)

s = p.read_text()

old_header = '''// server/src/routes_v5.cc
// routes_v5.cc
//
// v5 Stateless Login ("2A" flow) HTTP routes.
//
// Design goals
// - Stateless-ready correlation: clients can use `k = H(st)` (st_hash_b64) as the
//   primary lookup key. This avoids reliance on `sid` for v5.
// - Separation of concerns: this file is *transport + orchestration only*.
//   All crypto, token signing, storage, and auditing are delegated to callbacks
//   in RoutesV5Context (dependency injection).
// - Short-lived server-side state: the server keeps only minimal "pending" +
//   "approval" entries for UX and single-use consumption. These are pruned
//   aggressively by TTL to keep memory bounded.
//
// Flow summary
//   1) /api/v5/session
//      - Mint request token `st` (signed), return {sid, st, k, iat, exp, qr_svg}
//      - Insert PendingEntry keyed by `k` so status can immediately report "pending"
//   2) /api/v5/qr.svg
//      - Render QR containing dna://auth?v=5&st=...&origin=...&app=...
//   3) /api/v5/status (GET or POST)
//      - Resolve correlation key from {st|k|sid}
//      - Report {approved|pending|missing} with TTL metadata
//   4) /api/v5/consume
//      - Resolve correlation key from {st|k|sid}
//      - If approved, issue Set-Cookie(pqnas_session=...) and atomically consume
//        approval + pending entries (one-time use).
//
// Security notes
// - `st` is a signed request token. `k` is derived from st and is safe to expose.
// - `/consume` is intentionally strict: if approval exists but cookie is missing,
//   we fail loudly (500) because a partially-approved login is a server bug.
// - All JSON responses are no-store to avoid caching sensitive flow state.

'''

new_header = '''// server/src/routes_v5.cc
// routes_v5.cc
//
// Authentication, onboarding, and v5 QR-login HTTP routes.
//
// This file started as the v5 stateless QR-login route module. It now also
// contains password-login helpers, OPAQUE onboarding/reset orchestration,
// admin auth diagnostics, and compatibility/admin provisioning endpoints.
//
// Long-term cleanup target:
// - keep route handlers here
// - move OPAQUE enrollment-token storage into a dedicated module, e.g.
//   opaque_enrollments_store.{h,cpp}
// - keep this file as transport/orchestration, not storage/business logic
//
// v5 Stateless Login ("2A" flow) summary
//   1) /api/v5/session
//      - Mint request token `st` (signed), return {sid, st, k, iat, exp, qr_svg}
//      - Insert PendingEntry keyed by `k` so status can immediately report "pending"
//   2) /api/v5/qr.svg
//      - Render QR containing dna://auth?v=5&st=...&origin=...&app=...
//   3) /api/v5/status (GET or POST)
//      - Resolve correlation key from {st|k|sid}
//      - Report {approved|pending|missing} with TTL metadata
//   4) /api/v5/consume
//      - Resolve correlation key from {st|k|sid}
//      - If approved, issue Set-Cookie(pqnas_session=...) and atomically consume
//        approval + pending entries (one-time use).
//
// OPAQUE onboarding summary
// - Admin creates a setup/reset token.
// - Browser runs OPAQUE registration using the token.
// - Server stores only the OPAQUE server-side password file.
// - Plaintext passwords, password hashes, and fallback password fields must never
//   be accepted by OPAQUE endpoints.
// - Setup/reset tokens are single-use and are invalidated on replacement/revoke.
//
// Security notes
// - `st` is a signed request token. `k` is derived from st and is safe to expose.
// - `/consume` is intentionally strict: if approval exists but cookie is missing,
//   we fail loudly (500) because a partially-approved login is a server bug.
// - All JSON responses are no-store to avoid caching sensitive flow state.
// - Every read-modify-write of opaque_enrollments.json must hold the documented
//   lock discipline near RoutesV5OpaqueEnrollmentsFileLock.

'''

if old_header in s:
    s = s.replace(old_header, new_header, 1)
    print("updated file header")
else:
    print("header exact block not found; skipped header update")

# Fix accidental indentation in earlier comments.
s = s.replace(
'''// Security invariant:
    // opaque_enrollments.json is modified from both routes_v5.cc and main.cpp.
    // The in-process mutex below serializes routes_v5.cc threads, but it does
    // not protect main.cpp. This file-level flock coordinates all writers that
    // use the same opaque_enrollments.json.lock file.
class RoutesV5OpaqueEnrollmentsFileLock {
''',
'''// Security invariant:
//
// opaque_enrollments.json is modified from both routes_v5.cc and main.cpp.
// The in-process mutex below serializes routes_v5.cc threads, but it does not
// protect main.cpp. This file-level flock coordinates all writers that use the
// same opaque_enrollments.json.lock file.
//
// Lock ordering rule in routes_v5.cc:
//   1) routes_v5_opaque_enrollments_file_mu()
//   2) RoutesV5OpaqueEnrollmentsFileLock
//
// main.cpp uses only the file-level flock for the same enrollment file.
class RoutesV5OpaqueEnrollmentsFileLock {
'''
)

s = s.replace(
'''// Save opaque_enrollments.json using a temp-file + rename pattern.
    //
    // Important:
    // - caller must already hold the enrollment mutex/file lock
    // - temp filenames must be unique to avoid concurrent writer collisions
    // - rename on the same filesystem prevents readers from seeing a half-written file
static bool routes_v5_save_opaque_enrollments_no_lock(const std::string& path,
''',
'''// Save opaque_enrollments.json using a temp-file + rename pattern.
//
// Important:
// - caller must already hold the enrollment mutex/file lock
// - temp filenames must be unique to avoid concurrent writer collisions
// - rename on the same filesystem prevents readers from seeing a half-written file
static bool routes_v5_save_opaque_enrollments_no_lock(const std::string& path,
'''
)

insertions = [
    (
'''static std::string routes_v5_auth_mode() {
''',
'''// Resolve the active browser login mode.
//
// Note:
// PQNAS_AUTH_MODE=v5 is the older server auth-stack selector and must keep
// meaning QR/v5. New login UI selection should use PQNAS_LOGIN_MODE.
static std::string routes_v5_auth_mode() {
'''
    ),
    (
'''static bool routes_v5_has_forbidden_password_fallback_field(const nlohmann::json& j) {
''',
'''// OPAQUE endpoints must never accept plaintext passwords or classic fallback
// password-hash fields. The browser performs OPAQUE registration; the server
// stores only the resulting OPAQUE server-side password file.
static bool routes_v5_has_forbidden_password_fallback_field(const nlohmann::json& j) {
'''
    ),
    (
'''static bool routes_v5_opaque_backend_ready_for_registration(
    const pqnas::OpaqueBackendStatus& status) {
''',
'''// Registration readiness gate for OPAQUE setup/reset flows.
    //
    // This checks helper/server-setup/credential-store health before running
    // OPAQUE registration. It is intentionally stricter than a generic
    // "feature enabled" check.
static bool routes_v5_opaque_backend_ready_for_registration(
    const pqnas::OpaqueBackendStatus& status) {
'''
    ),
    (
'''static std::string routes_v5_opaque_enrollments_path(const RoutesV5Context& ctx) {
''',
'''// Resolve the shared OPAQUE enrollment-token store path.
    //
    // main.cpp must use the same path derivation for user revoke invalidation,
    // otherwise revoke and onboarding would update different files.
static std::string routes_v5_opaque_enrollments_path(const RoutesV5Context& ctx) {
'''
    ),
    (
'''static std::size_t routes_v5_invalidate_active_opaque_enrollment_tokens(json& doc,
''',
'''// Mark active setup/reset tokens as used/invalidated.
    //
    // Empty login or fingerprint means wildcard for that field. This is used for:
    // - replacement: login + fingerprint
    // - user revoke from main.cpp: fingerprint-only equivalent
    //
    // Only active tokens are touched: used_at == 0 and expires_at > now.
static std::size_t routes_v5_invalidate_active_opaque_enrollment_tokens(json& doc,
'''
    ),
    (
'''static bool routes_v5_opaque_login_pending_put(
''',
'''// Store short-lived OPAQUE login-start state.
    //
    // This is intentionally memory-only and TTL-bounded. The corresponding pop()
    // below consumes the entry once so login state cannot be replayed.
static bool routes_v5_opaque_login_pending_put(
'''
    ),
]

for old, new in insertions:
    if old in s and new not in s:
        s = s.replace(old, new, 1)

p.write_text(s)
print("done")
