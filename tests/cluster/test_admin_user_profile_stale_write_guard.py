#!/usr/bin/env python3

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

ROUTE = (
    ROOT
    / "server/src/routes/routes_admin_user_profile.cpp"
).read_text()

START = 'srv.Post("/api/v4/admin/users/upsert"'
END = 'srv.Post("/api/v4/admin/users/enable"'

start = ROUTE.find(START)
end = ROUTE.find(END, start + len(START))

assert start >= 0, "admin users upsert route not found"
assert end > start, "admin users upsert route boundary not found"

route = ROUTE[start:end]

# Security: RT-005 was caused by copying a complete UserRec, then writing that
# stale snapshot back after a concurrent placement/projection mutation.
assert "set_profile_persisted(" in route, (
    "admin profile update must use profile-only persisted mutation"
)

for forbidden in (
    "users->upsert(",
    "users->upsert_persisted(",
    "users->save(",
    "existing_user_for_immutable_fp",
):
    assert forbidden not in route, (
        "RT-005 stale UserRec write-back pattern returned: "
        + forbidden
    )

print("admin user profile stale-write guard passed")
