#!/usr/bin/env python3

from pathlib import Path

status_route = Path(
    "server/src/routes/routes_admin_user_status.cpp"
).read_text()

lifecycle_route = Path(
    "server/src/routes/routes_admin_user_lifecycle.cpp"
).read_text()

v5_route = Path(
    "server/src/routes/routes_v5.cc"
).read_text()

errors = []


def require(condition, message):
    if not condition:
        errors.append(message)


# Admin status route.
require(
    "c.users->set_status_persisted(" in status_route,
    "admin status route must use write-through status persistence",
)

require(
    "c.users->set_status(fp, status)" not in status_route,
    "admin status route must not use in-memory set_status + later save",
)

# Admin lifecycle disable route.
require(
    "ctx.users->set_status_persisted(" in lifecycle_route,
    "lifecycle disable must use write-through status persistence",
)

require(
    'ctx.users->set_status(fp, "disabled")' not in lifecycle_route,
    "lifecycle disable must not use in-memory set_status + later save",
)

# Existing-user V5 enable paths previously copied the whole UserRec and then
# performed upsert()+save(). That creates a split projection/revision commit.
old_v5 = (
    "ctx.users->upsert(updated_user) || "
    "!ctx.users->save(*ctx.users_path)"
)

require(
    old_v5 not in v5_route,
    "V5 existing-user enable paths must not use upsert + save",
)

require(
    v5_route.count("set_status_persisted(") >= 2,
    "V5 existing-user enable paths must use persisted status mutation",
)

if errors:
    for error in errors:
        print(f"FAIL: {error}")

    raise SystemExit(1)

print("OK: projection status persisted-caller guards passed")
