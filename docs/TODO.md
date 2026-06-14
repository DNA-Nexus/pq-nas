# DNA-Nexus TODO

This file collects follow-up items discovered during testing and documentation work.


## Auth / admin consistency: `users.json` vs `policy.json`

Date: 2026-06-14

### Problem

QR / DNA Connect login can create or enable an admin user in `users.json`, but some older admin routes still check the legacy allowlist in `policy.json`.

Observed symptom:

- QR login succeeds.
- The user appears as `role: admin` and `status: enabled` in `/etc/pqnas/users.json`.
- Some admin UI buttons work.
- Some admin UI buttons still return `admin required`.
- `/etc/pqnas/policy.json` may contain an empty allowlist.

Example empty policy file:

    {
      "users": []
    }

### Likely cause

There are currently two admin authorization paths.

Newer path:

- `require_admin_cookie_users(...)`
- `require_admin_cookie_users_actor(...)`
- checks `users.json`

Legacy path:

- `require_admin_cookie(...)`
- `is_admin_cookie(...)`
- checks `policy.json` / allowlist

This causes inconsistent admin behavior after QR / DNA Connect login.

### Temporary workaround

Sync enabled admin users from `users.json` into `policy.json`.

### Proper fix later

Make `users.json` / `UsersRegistry` the single source of truth for user status and admin role.

Possible implementation options:

1. Migrate remaining admin routes away from legacy allowlist checks.
2. Replace old `require_admin_cookie(...)` usage with `require_admin_cookie_users(...)` or `require_admin_cookie_users_actor(...)`.
3. Replace admin-only UI checks using `is_admin_cookie(... allowlist ...)` with a users-registry based check.
4. As a transition safety net, sync enabled admins from `users.json` to `policy.json` on startup.
5. Add installer/login warning when a QR/DNA Connect admin exists in `users.json` but not in legacy policy allowlist.

### Search hints

Useful searches:

    rg -n 'require_admin_cookie\(|is_admin_cookie\(|allowlist|policy.json' server/src
    rg -n 'require_admin_cookie_users|require_admin_cookie_users_actor|is_admin_enabled' server/src

### Notes

This should be fixed before release because admin behavior must be consistent regardless of login method.
