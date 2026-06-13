# OPAQUE onboarding red team report

## Reviewed commit

```
72133cc5bec170ec395d9ea09a5b0577f505547e
```

## Executive summary

The OPAQUE onboarding and admin-created user flow is well-designed overall. Token hashing with SHA-256, OPAQUE zero-knowledge password setup, `sodium_memzero` for secret wiping, login normalization, and comprehensive audit logging are all correctly implemented. The most significant findings are: (1) the enrollment token is not invalidated when a user is revoked — the revoke check happens at token use time but the token remains active, creating a race window; (2) the force-reset flow is a two-step non-atomic operation on the client side that can leave the user in a broken state if the second call fails; (3) the `opaque_enrollments.json` save is not atomic (uses direct `ofstream` truncation, unlike the atomic-rename pattern used for `opaque_credentials.json` and `users.json`); (4) the `opaque-create` endpoint accepts `status: "enabled"` which allows creating a user that is enabled before OPAQUE enrollment is complete. No critical authentication bypasses were found.

---

## Findings

### F-1: Token not invalidated on user revoke — race window

- **Severity:** Medium
- **Location:** `routes_v5.cc` — `POST /api/v4/admin/users/status` (revoke action) and `POST /api/auth/opaque/enrollment/start` / `enrollment/finish` (lines 1953, 2110)
- **Problem:** When an admin revokes a user via the Approvals UI, only `users.json` status is set to `"revoked"`. Any outstanding active enrollment token in `opaque_enrollments.json` is **not** invalidated or deleted. The enrollment endpoints check `user->status == "revoked"` at use time, which is correct, but there is a TOCTOU window: between the moment the revoke call completes and the moment the enrollment endpoint re-reads users.json, a concurrent enrollment/finish request that already loaded the user record could proceed. More importantly, if the admin later changes the user status back to `"disabled"` (un-revokes), the old token becomes usable again without the admin realizing it.
- **Exploit scenario:** Admin creates OPAQUE user, issues setup link, then revokes the user. Admin later un-revokes (sets to "disabled") to "park" the identity. The old setup token (if within its 24h TTL) is now usable by whoever received the original link.
- **Recommended fix:** When revoking a user, also mark all active enrollment tokens for that fingerprint as expired or used. Alternatively, add a `user_status_at_validation` check that verifies the user was not revoked and re-enabled between token issuance and use.
- **Minimal patch idea:** In the status-change handler for revoke, add a call that loads `opaque_enrollments.json`, iterates tokens matching the fingerprint where `used_at == 0`, sets `used_at = now` or `expires_at = now`, and saves.

---

### F-2: Force-reset is two separate HTTP calls — non-atomic operation

- **Severity:** Medium
- **Location:** `admin_approvals.js:244-277` (`forceOpaqueReset()`) and `routes_v5.cc` lines 2235, 1687
- **Problem:** The force-reset UI performs two sequential API calls: (1) `POST /api/admin/auth/opaque/credential/disable` to disable the OPAQUE credential, then (2) `POST /api/admin/auth/opaque/enrollment-token/create` to issue a new reset link. If the second call fails (network error, server error, admin closes tab), the credential is disabled but no reset token exists. The user is locked out with no way to recover except the admin retrying manually. There is no server-side "force-reset" transaction that combines both steps.
- **Exploit scenario:** Network hiccup after step 1. User is permanently locked out. Admin sees the user in "reset_required" state but may not understand they need to manually create a new reset link.
- **Recommended fix:** Either (a) create a server-side `POST /api/admin/auth/opaque/force-reset` endpoint that atomically disables the credential and creates the reset token in one transaction, or (b) if step 2 fails, have the client automatically retry or re-enable the credential as a rollback.
- **Minimal patch idea:** Add a combined `force-reset` endpoint that wraps credential-disable + token-create in a single handler with rollback on failure.

---

### F-3: Enrollment file save is not atomic — data loss risk

- **Severity:** Medium
- **Location:** `routes_v5.cc:940-965` (`routes_v5_save_opaque_enrollments_no_lock`)
- **Problem:** The enrollment token store is saved by opening the file with `std::ios::trunc` and writing directly — not using the atomic write-to-tmp-then-rename pattern. Both `OpaqueCredentials::save()` and `UsersRegistry::save()` correctly use atomic rename. A crash or power loss during the enrollment save could corrupt or zero out the file, losing all active tokens.
- **Exploit scenario:** A power failure during enrollment save truncates `opaque_enrollments.json`. All active setup tokens are lost. Users with pending setup links cannot complete enrollment. Not directly exploitable for auth bypass, but degrades availability and requires admin intervention.
- **Recommended fix:** Use the same atomic write pattern: write to a temp file in the same directory, then `std::filesystem::rename`.
- **Minimal patch idea:** Change `routes_v5_save_opaque_enrollments_no_lock` to write to `path + ".tmp." + pid + "." + timestamp`, then rename over the target path (matching the pattern in `OpaqueCredentials::save()`).

---

### F-4: `opaque-create` accepts `status: "enabled"` — pre-enrollment enabled user

- **Severity:** Medium
- **Location:** `routes_v5.cc:4221-4238` (`POST /api/admin/users/opaque-create`)
- **Problem:** The endpoint accepts status values `"enabled"`, `"disabled"`, and `"pending"` from the admin's request. If an admin (or a compromised admin session) creates a user with `status: "enabled"`, that user record exists in `users.json` as enabled despite having no OPAQUE credential. The login endpoints correctly check for credential existence, so this does not directly allow login. However, other parts of the system that check `is_enabled_user()` may grant filesystem or API access to this fingerprint prematurely. The default is `"disabled"`, and the frontend hardcodes the default, so this only matters for direct API calls.
- **Exploit scenario:** A rogue admin creates a user with `status: "enabled"` and `role: "admin"`, then uses that fingerprint in other API contexts that trust `is_enabled_user()` without checking OPAQUE credential presence.
- **Recommended fix:** Force `status = "disabled"` in the opaque-create endpoint, ignoring client-supplied values. The enrollment-finish step should be the only path to enabling the user.
- **Minimal patch idea:** Remove the status parameter acceptance or hardcode `status = "disabled"` before the `UserRec` is built.

---

### F-5: `opaque-create` also accepts `"pending"` status — not a recognized status

- **Severity:** Low
- **Location:** `routes_v5.cc:4235`
- **Problem:** The validation allows `status == "pending"`, but `UsersRegistry::norm_status()` only recognizes `"enabled"`, `"disabled"`, and `"revoked"`. The value `"pending"` passes the route validation but when persisted through `upsert()` -> `norm_status()`, it normalizes to `"disabled"`. This means the validation and the actual stored value disagree. If `norm_status` were later updated to accept `"pending"`, this silent acceptance could become a problem.
- **Exploit scenario:** No direct exploit. Inconsistency between validation layer and storage layer.
- **Recommended fix:** Only accept `"enabled"` and `"disabled"` in the route validation (matching the norm_status whitelist), or remove status acceptance entirely per F-4.
- **Minimal patch idea:** Change the validation to `if (status != "enabled" && status != "disabled")` or simply hardcode `"disabled"`.

---

### F-6: No CSRF token on admin POST endpoints — relies solely on SameSite=Strict

- **Severity:** Low
- **Location:** All admin POST endpoints (`/api/admin/auth/opaque/*`, `/api/admin/users/opaque-create`, `/api/v4/admin/users/*`)
- **Problem:** Admin endpoints rely on `pqnas_session` cookie with `SameSite=Strict` for CSRF protection. This is effective in modern browsers but provides no protection in older browsers that do not support `SameSite`. There is no explicit CSRF token, `Origin` header check, or custom header requirement. The `Content-Type: application/json` requirement provides partial protection (simple form POSTs cannot set this header), but `fetch()` from a cross-origin page can if CORS is misconfigured.
- **Exploit scenario:** An attacker on an older browser (pre-SameSite support, which is very rare in 2026) or in a context where SameSite is not enforced could forge admin requests. Practically very low risk.
- **Recommended fix:** Add an `Origin` header check on all mutating admin endpoints as defense-in-depth. Verify that `req.get_header_value("Origin")` matches the expected server origin, or that the `Referer` header starts with the expected origin.
- **Minimal patch idea:** At the top of each admin POST handler, add: `if (Origin header present && does not match Host) -> 403`.

---

### F-7: Login username stored in localStorage

- **Severity:** Low
- **Location:** `auth_login.js:475`, `admin_settings.js:618`, `app.js:2252`, `external_workspace.js:828`
- **Problem:** The login/email is stored in `localStorage` as `pqnas_opaque_login` / `pqnas_password_login` for UX convenience (pre-filling the login field). This is a username, not a secret, but on shared or kiosk devices it reveals which accounts have been used. No passwords, tokens, or secrets are stored in localStorage/sessionStorage — this is good.
- **Exploit scenario:** On a shared device, a subsequent user can see which login was previously used by inspecting localStorage.
- **Recommended fix:** Consider offering a "Remember my login" checkbox that defaults to off, rather than always persisting the username. Alternatively, accept this as a minor UX trade-off.
- **Minimal patch idea:** Gate `localStorage.setItem("pqnas_opaque_login", ...)` behind a user preference.

---

### F-8: Recovery words returned in API response body — server memory residency

- **Severity:** Low
- **Location:** `routes_v5.cc:4310-4346` (`POST /api/admin/users/opaque-create`)
- **Problem:** Recovery words are generated server-side and returned in the JSON response to the admin. The code correctly wipes `ident.recovery_words` and `recovery_words_json` with `sodium_memzero` after building the response. However, the response body string is held in `httplib`'s internal buffers and likely also in the admin's browser memory (JavaScript heap via the `fetch` response). The server does call `routes_v5_secure_clear_string(response_body)` after `reply_json`, but `reply_json` has already copied the data into httplib's response buffer which is not wiped.
- **Exploit scenario:** A memory-dump attack on the server process (core dump, `/proc/pid/mem`) shortly after user creation could recover the recovery words from httplib's response buffers. Practically requires root access on the server.
- **Recommended fix:** This is an inherent trade-off of server-side identity generation. Document that the recovery words exist transiently in server memory. Consider using a streaming response with explicit wiping of the httplib buffer if the library supports it, or generating the identity client-side.
- **Minimal patch idea:** Difficult to fully mitigate without client-side key generation. Current `sodium_memzero` usage is already best-effort.

---

### F-9: `enrollment/finish` marks token used AFTER saving credentials — partial completion risk

- **Severity:** Low
- **Location:** `routes_v5.cc:2174-2208` (`POST /api/auth/opaque/enrollment/finish`)
- **Problem:** The enrollment finish handler: (1) saves the OPAQUE credential to `opaque_credentials.json`, (2) enables the user in `users.json`, (3) marks the token as used in `opaque_enrollments.json`. If step 3 fails (disk error), the credential is saved and user is enabled, but the token is not marked as used. The endpoint returns 500, but the credential is already stored. On retry, the token is still marked unused, so the user (or an attacker) could re-run enrollment/finish with a different password, overwriting the credential. This is a very narrow race but could allow credential replacement.
- **Exploit scenario:** An attacker intercepts the network response, sees the 500 error, and replays the enrollment with their own OPAQUE registration upload, replacing the legitimate user's credential.
- **Recommended fix:** Mark the token as used **before** saving the credential, or wrap the three saves in a best-effort transaction (save enrollment first, then credential, then user-enable; roll back enrollment on credential save failure).
- **Minimal patch idea:** Move `(*token_rec)["used_at"] = now; save_enrollments(...)` before `creds.upsert(cred); creds.save(...)`.

---

### F-10: No old token revocation on new token creation

- **Severity:** Low
- **Location:** `routes_v5.cc:1806-1817` (`POST /api/admin/auth/opaque/enrollment-token/create`)
- **Problem:** When an admin creates a new enrollment token for a user (e.g., a new setup link or a new reset link), old active tokens for the same login/fingerprint are not invalidated. Both old and new tokens remain valid until their independent TTLs expire. This means if an admin creates a second setup link (perhaps because the first one was compromised), the first one still works.
- **Exploit scenario:** Admin suspects the first setup link was intercepted, creates a new one. An attacker who intercepted the first link can still use it within its TTL.
- **Recommended fix:** When creating a new enrollment token, iterate existing tokens for the same login+fingerprint and set `used_at = now` on any active (unexpired, unused) ones.
- **Minimal patch idea:** Before `doc["tokens"].push_back(...)`, loop through `doc["tokens"]` and for each entry matching login + fingerprint where `used_at == 0 && expires_at > now`, set `rec["used_at"] = now`.

---

### F-11: Name and notes fields not length-limited

- **Severity:** Info
- **Location:** `routes_v5.cc:4200-4201` (`POST /api/admin/users/opaque-create`), `users_registry.cpp:462-468` (`set_name_notes`)
- **Problem:** The `name` field from `opaque-create` is trimmed but not length-limited. A malicious admin could set an extremely long name (megabytes). Similarly, `set_name_notes` does not validate length. The `login` field is correctly limited to 254 characters.
- **Exploit scenario:** Denial-of-service through oversized user records bloating `users.json`. Requires admin access, so severity is low.
- **Recommended fix:** Add length limits (e.g., 256 chars for name, 1024 for notes) in the route handlers.
- **Minimal patch idea:** `if (name.size() > 256) return 400`.

---

### F-12: Bootstrap admin recovery words are returned and wiped, but the endpoint has no token scope limit

- **Severity:** Info
- **Location:** `routes_v5.cc:2840-3200` (`POST /api/auth/opaque/bootstrap-admin/start` and `/finish`)
- **Problem:** The bootstrap endpoint is protected by `PQNAS_OPAQUE_BOOTSTRAP_TOKEN` (environment variable) and only works when no enabled admin exists — both good. However, the bootstrap token has no TTL or usage limit beyond the "no enabled admin" check. If the environment variable persists after initial setup, and an admin is later deleted (e.g., last admin deletes themselves), the bootstrap endpoint reopens. This is by design but worth noting.
- **Exploit scenario:** An attacker who learns the bootstrap token value and can arrange for all admins to be deleted could create a new admin. Requires knowledge of the env var and ability to remove all admins.
- **Recommended fix:** Document clearly that `PQNAS_OPAQUE_BOOTSTRAP_TOKEN` should be unset or rotated after initial setup. Consider adding a "bootstrap used" flag in persistent state.
- **Minimal patch idea:** After successful bootstrap, write a marker file (e.g., `.opaque_bootstrap_completed`) and refuse bootstrap if it exists.

---

## Things that look safe

1. **Token hashing:** Enrollment tokens are stored as SHA-256 hashes only (`sha256_hex(token)` at line 1791). The plaintext token is returned once to the admin and never stored. This is correctly implemented.

2. **No plaintext password transmission:** The `opaque-enroll.html` page runs OPAQUE registration entirely in-browser via WASM. The password is never sent to the server — only `registration_request_b64` and `registration_upload_b64` (OPAQUE protocol messages) are transmitted. The server-side `contains_forbidden_password_fallback_field()` check rejects any request containing `password`, `plaintext_password`, etc. fields.

3. **Revoked user check at enrollment:** Both `enrollment/start` (line 1953) and `enrollment/finish` (line 2110) check `user->status == "revoked"` and reject with 403. A revoked user cannot complete enrollment.

4. **Credential enabled check at login:** `login/start` (line 2604-2607) checks `rec->enabled` and rejects disabled credentials. The force-reset flow correctly sets `rec.enabled = false` (line 2324), so the old password stops working immediately.

5. **User enabled check at login:** `login/start` (line 2619) and `login/finish` (line 2788) both verify `user->status == "enabled"`. Disabled or revoked users cannot log in even if they have valid OPAQUE credentials.

6. **Rate limiting:** All public endpoints (`enrollment/start`, `enrollment/finish`, `login/start`, `login/finish`, `bootstrap`) have IP-based rate limiting (12-20 requests per 60 seconds).

7. **Audit logging:** Every security-sensitive action (create, enable, disable, revoke, delete, credential-disable, enrollment-token-create, enrollment-start, enrollment-finish, login-start, login-finish) generates audit events with actor fingerprint, target login/fingerprint, and outcome.

8. **Session cookie security:** `pqnas_session` is set with `HttpOnly; SameSite=Strict; Secure; Path=/` — preventing JavaScript access and cross-site request attachment.

9. **Secret wiping:** Recovery words, mnemonic buffers, signing seeds, encryption seeds, and secret keys are wiped with `sodium_memzero` after use in `dna_identity_generator.cpp`. Response bodies containing recovery words are wiped with `routes_v5_secure_clear_string` after sending.

10. **Input validation:** Login, fingerprint, base64 fields, and suite strings all have length limits, character-class validation (`routes_v5_is_safe_enrollment_token`, `routes_v5_is_safe_b64ish`, `routes_v5_has_control_chars`), and the forbidden-password-fallback-field check provides defense against downgrade attacks.

11. **Normalization firewall:** `UsersRegistry` normalizes role (defaults to `"user"`), status (defaults to `"disabled"`), and storage_state (defaults to `"unallocated"`) — fail-closed defaults that prevent privilege escalation through malformed data.

12. **Atomic file writes:** Both `opaque_credentials.json` and `users.json` use the write-to-temp-then-rename pattern, preventing corruption from partial writes.

13. **Frontend XSS protection:** The `esc()` function in `admin_approvals.js` escapes `& < > " '` before inserting into innerHTML. All user-controlled values (fingerprint, name, login, notes) are escaped.

14. **Token single-use enforcement:** `enrollment/finish` sets `used_at = now` on the token record and `enrollment/start` checks `used_at > 0` to reject already-used tokens.

15. **User status change detection:** `enrollment/finish` (lines 2116-2121) checks `user_status_at_issue` against the current status, rejecting enrollment if the status changed unexpectedly (except if already enabled).

---

## Recommended next steps

1. **[Medium] Invalidate enrollment tokens on user revoke (F-1).** When setting a user's status to `"revoked"`, also expire all active enrollment tokens for that user. This closes the revoke-then-un-revoke reactivation window.

2. **[Medium] Revoke old tokens when creating new ones (F-10).** When an admin creates a new enrollment/reset token for a user, invalidate any previous active tokens for the same login+fingerprint.

3. **[Medium] Make force-reset atomic or add rollback (F-2).** Either create a combined server-side force-reset endpoint, or have the client roll back the credential disable if token creation fails.

4. **[Medium] Use atomic save for `opaque_enrollments.json` (F-3).** Apply the same write-to-temp-then-rename pattern used by the other JSON stores.

5. **[Medium] Reject or ignore `status` parameter in `opaque-create` (F-4).** Force new OPAQUE users to start as `"disabled"` regardless of the request, since they cannot log in before enrollment anyway.

6. **[Low] Mark token used before saving credential (F-9).** Reorder the saves in `enrollment/finish` so the token is consumed first, preventing credential replacement on retry.

7. **[Low] Add `Origin` header validation as CSRF defense-in-depth (F-6).** Supplement `SameSite=Strict` with an explicit origin check.

8. **[Low] Add length limits on name/notes fields (F-11).** Prevent oversized user records.

9. **[Low] Consider making login storage opt-in (F-7).** Let users choose whether their username is remembered in localStorage.

10. **[Info] Document bootstrap token lifecycle (F-12).** Recommend unsetting `PQNAS_OPAQUE_BOOTSTRAP_TOKEN` after initial setup in deployment documentation.
