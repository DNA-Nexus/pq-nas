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

## My Activity should not depend on personal storage allocation

Workspace-only users can exist without a personal file storage allocation. For example,
an OPAQUE-provisioned user may be enabled and able to accept a shared workspace invite,
but still have personal storage marked as unallocated.

Observed problem:
- The user can receive a pending workspace invitation.
- The user can accept the invitation.
- The user can enter or leave the workspace.
- My Activity does not show these events when the user's personal storage/root is not allocated.

Reason:
- My Activity currently stores/reads activity from the user's personal storage root.
- If the user has no allocated personal storage, there may be no user root or activity DB.

Desired fix:
- Move activity logging to account-level/system metadata instead of user quota storage.
- Suggested location: /var/lib/pqnas/activity/users/<fingerprint>/activity.sqlite
  or /srv/pqnas/.system/activity/users/<fingerprint>/activity.sqlite.
- Activity metadata should not consume or require the user's personal file quota.

Events that should be recorded:
- workspace.invite.sent
- workspace.invite.accepted
- workspace.invite.declined
- workspace.left
- workspace.member.removed
- workspace.member.role_changed

Acceptance criteria:
- A user with storage_state=unallocated can still see My Activity.
- Accepting a workspace invite appears in My Activity.
- Declining a workspace invite appears in My Activity.
- Leaving a workspace appears in My Activity.
- Existing allocated-storage users keep seeing their previous activity or a migration/compatibility path exists.

