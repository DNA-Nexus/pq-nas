#include "users_registry.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iostream>
#include <unistd.h>

using json = nlohmann::json;

namespace pqnas {

/*
================================================================================
Users Registry — Architectural Overview
================================================================================

Purpose
-------
UsersRegistry is PQ-NAS's in-process authoritative source of user metadata,
backed by a JSON file on disk.

It stores and serves:
  - Identity: fingerprint (primary key)
  - Authorization: role (admin/user) and status (enabled/disabled/revoked)
  - Profile metadata: name, group, email, address, avatar_url, notes
  - Storage metadata: storage_state, quota_bytes, root_rel, and audit fields
  - Activity: added_at and last_seen (ISO8601 strings in PQ-NAS conventions)

This module is designed to be:
  - Simple: JSON file with a stable schema.
  - Backward-compatible: missing fields default to safe values.
  - Thread-safe: internal state is guarded by a mutex.

Data model
----------
Primary key:
  - fingerprint (fp_hex string)

Core fields:
  - role: "admin" | "user"
  - status: "enabled" | "disabled" | "revoked"
  - storage_state: "allocated" | "unallocated"

Storage metadata (important for quotas + File Manager):
  - quota_bytes: uint64 (0 is treated as "deny positive uploads" by quota code)
  - root_rel: per-user relative root directory for data
  - storage_set_at / storage_set_by: audit trace for allocations

Threading model
---------------
  - by_fp_ is protected by mu_
  - All public methods either take the lock directly or call a method that does.
  - snapshot() provides a full copy for callers that need stable iteration
    without holding the lock.

Persistence model
-----------------
  - load(path): reads the JSON file and replaces in-memory state.
  - save(path): writes a complete snapshot to a temp file in the same directory,
    then renames tmp -> target for atomic replace.

Durability notes:
  - save() uses flush(), but does not fsync() file or parent directory.
    If you need power-loss durability, consider fsync(tmp_fd) + fsync(dir_fd).

Security / correctness notes
----------------------------
  - Normalization functions (norm_role/norm_status/norm_storage_state) provide
    a schema firewall: only known values are accepted; unknown values map to
    conservative defaults (e.g., disabled/unallocated).
  - safe_u64_from_json is defensive against signed/float encodings.

Potential issues to be aware of
-------------------------------
  - load() currently returns false if file is missing/unreadable.
    Some registry designs treat missing as "empty registry" and return true.
    Ensure higher-level code handles this according to your intended UX.
  - save(): "address" is currently written twice in the JSON initializer.
    This is likely a copy/paste mistake; it can confuse readers and may mask bugs.
================================================================================
*/


/*
valid_user_home_node_id()
  - Empty means local/standalone placement.
  - Non-empty values use the same narrow shape as stable cluster node IDs.
  - Paths, URLs, whitespace and traversal-like values are rejected.
*/
bool valid_user_home_node_id(
    const std::string& value
) {
  if (value.empty()) return true;
  if (value.size() > 64) return false;

  for (std::size_t index = 0;
       index < value.size();
       ++index) {
    const unsigned char ch =
        static_cast<unsigned char>(value[index]);

    const bool alphanumeric =
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9');

    if (index == 0) {
      if (!alphanumeric) return false;
      continue;
    }

    if (!alphanumeric &&
        ch != '-' &&
        ch != '_' &&
        ch != '.') {
      return false;
    }
  }

  return true;
}

/*
valid_user_placement_policy_id()
  - Empty means no policy override.
  - Non-empty values use the stable control-plane identifier shape.
  - Paths, URLs, whitespace and traversal-like values are rejected.
*/
bool valid_user_placement_policy_id(
    const std::string& value
) {
  if (value.empty()) return true;
  if (value.size() > 64) return false;

  for (std::size_t index = 0;
       index < value.size();
       ++index) {
    const unsigned char ch =
        static_cast<unsigned char>(value[index]);

    const bool alphanumeric =
        (ch >= 'A' && ch <= 'Z') ||
        (ch >= 'a' && ch <= 'z') ||
        (ch >= '0' && ch <= '9');

    if (index == 0) {
      if (!alphanumeric) return false;
      continue;
    }

    if (!alphanumeric &&
        ch != '-' &&
        ch != '_' &&
        ch != '.') {
      return false;
    }
  }

  return true;
}



//------------------------------------------------------------------------------
// Normalization helpers (schema firewall)
//------------------------------------------------------------------------------

/*
norm_role()
  - Enforces a two-role model: "admin" or "user".
  - Unknown input collapses to "user" (least privilege).
*/
static std::string norm_role(std::string r) {
  if (r == "admin") return "admin";
  return "user";
}

/*
norm_status()
  - Enforces allowed account statuses.
  - Unknown input collapses to "disabled" (fail-closed).
  - This helps prevent unexpected JSON edits from granting access.
*/
static std::string norm_status(std::string s) {
  if (s == "enabled" || s == "disabled" || s == "revoked") return s;
  return "disabled";
}

/*
norm_storage_state()
  - Enforces allowed storage states.
  - Unknown input collapses to "unallocated" (fail-closed).
*/
static std::string norm_storage_state(std::string s) {
  if (s == "allocated") return "allocated";
  return "unallocated";
}

/*
norm_vault_master_recovery_status()
  - Enforces the per-user Vault Master recovery lifecycle.
  - Unknown values collapse to not_configured so bad JSON edits cannot enable
    recovery accidentally.
*/
static std::string norm_vault_master_recovery_status(std::string s) {
  if (s == "active" || s == "disabled") return s;
  return "not_configured";
}


//------------------------------------------------------------------------------
// JSON decoding helper (defensive numeric parsing)
//------------------------------------------------------------------------------

/*
safe_u64_from_json(obj, key, def)
  - Reads an integer-like field from JSON and returns it as uint64_t.
  - Accepts:
      * unsigned integers
      * signed integers (>=0)
      * floats (>=0 and within range)
  - Anything invalid returns def.
  - This preserves backward compatibility and tolerates accidental type drift.
*/
static std::uint64_t safe_u64_from_json(const json& obj, const char* key, std::uint64_t def) {
  if (!obj.is_object() || !obj.contains(key)) return def;
  const auto& v = obj.at(key);
  // nlohmann can store numbers as integer/unsigned; be defensive.
  try {
    if (v.is_number_unsigned()) return v.get<std::uint64_t>();
    if (v.is_number_integer()) {
      auto x = v.get<std::int64_t>();
      return (x < 0) ? def : static_cast<std::uint64_t>(x);
    }
    if (v.is_number_float()) {
      double x = v.get<double>();
      if (x < 0) return def;
      if (x > static_cast<double>(std::numeric_limits<std::uint64_t>::max())) return def;
      return static_cast<std::uint64_t>(x);
    }
  } catch (...) {
  }
  return def;
}


//------------------------------------------------------------------------------
// Persistence: load()
//------------------------------------------------------------------------------

/*
UsersRegistry::load(path)
  - Replaces current in-memory registry with contents of the JSON file.
  - This is a full snapshot load (not incremental).

Error handling strategy:
  - Returns false if:
      * file cannot be opened
      * JSON is malformed / schema mismatch
  - Skips malformed user entries, but keeps loading others.

Backward compatibility:
  - New fields are read with defaults so older files remain valid.
  - Normalization is applied so unexpected values do not escalate privilege.

NOTE:
  - This function currently returns false if the file is missing.
    If you want "missing file = empty registry" semantics (like ShareRegistry),
    adjust this behavior and ensure callers match the intended UX.
*/
bool UsersRegistry::load(const std::string& path) {
  std::lock_guard<std::mutex> lk(mu_);
  by_fp_.clear();

  std::ifstream f(path);
  if (!f.good()) return false;

  json j;
  f >> j;
  if (!j.is_object() || !j.contains("users") || !j["users"].is_array()) return false;

  for (auto& it : j["users"]) {
    if (!it.is_object()) continue;

    UserRec u;
    u.fingerprint = it.value("fingerprint", "");
    if (u.fingerprint.empty()) continue;

    // Existing
    u.name      = it.value("name", "");
    u.role      = norm_role(it.value("role", "user"));
    u.status    = norm_status(it.value("status", "disabled"));
    u.added_at  = it.value("added_at", "");
    u.last_seen = it.value("last_seen", "");
    u.notes     = it.value("notes", "");

    // New: profile
    u.group   = it.value("group", "");
    u.email   = it.value("email", "");
    u.address = it.value("address", "");
    u.avatar_url = it.value("avatar_url", "");

    if (it.contains("app_prefs") && it["app_prefs"].is_object()) {
      u.app_prefs_json = it["app_prefs"].dump();
    } else {
      u.app_prefs_json = "{}";
    }

    // Vault Master recovery is per-user/fingerprint. Only public key metadata is
    // loaded; no private recovery key material belongs in users.json.
    if (it.contains("vault_master_recovery") && it["vault_master_recovery"].is_object()) {
      const auto& vr = it["vault_master_recovery"];
      u.vault_master_recovery_status =
          norm_vault_master_recovery_status(vr.value("status", "not_configured"));
      u.vault_master_recovery_public_key_b64 = vr.value("public_key_b64", "");
      u.vault_master_recovery_public_key_sha256 = vr.value("public_key_sha256", "");
      if (vr.contains("created_at") && vr["created_at"].is_number_integer()) {
        u.vault_master_recovery_created_at = vr["created_at"].get<long long>();
        if (u.vault_master_recovery_created_at < 0) u.vault_master_recovery_created_at = 0;
      } else {
        u.vault_master_recovery_created_at = 0;
      }
      u.vault_master_recovery_label = vr.value("label", "");
    } else {
      u.vault_master_recovery_status = "not_configured";
      u.vault_master_recovery_public_key_b64 = "";
      u.vault_master_recovery_public_key_sha256 = "";
      u.vault_master_recovery_created_at = 0;
      u.vault_master_recovery_label = "";
    }

    // New: storage metadata (defaults for backward compatibility)
    u.storage_state  = norm_storage_state(it.value("storage_state", "unallocated"));
    u.quota_bytes    = safe_u64_from_json(it, "quota_bytes", 0);
    u.root_rel       = it.value("root_rel", "");
    u.storage_pool_id = it.value("storage_pool_id", "");
    u.storage_set_at = it.value("storage_set_at", "");
    u.storage_set_by = it.value("storage_set_by", "");

    // Missing home_node is the backward-compatible standalone representation.
    // Security: malformed non-empty placement metadata fails registry loading
    // rather than silently becoming a local user on a Director.
    if (it.contains("home_node")) {
      if (!it["home_node"].is_string()) return false;

      u.home_node =
          it["home_node"].get<std::string>();

      if (!valid_user_home_node_id(u.home_node)) {
        return false;
      }
    } else {
      u.home_node = "";
    }

    // Missing projection revision is the backward-compatible representation
    // for registries written before Director -> Storage projection versioning.
    //
    // Security: explicit malformed or negative revisions fail closed rather
    // than silently resetting a Director-owned monotonic authorization version.
    if (it.contains("storage_projection_revision")) {
      const auto& revision_json =
          it["storage_projection_revision"];

      if (revision_json.is_number_unsigned()) {
        try {
          u.storage_projection_revision =
              revision_json.get<std::uint64_t>();
        } catch (...) {
          return false;
        }
      } else if (revision_json.is_number_integer()) {
        try {
          const std::int64_t signed_revision =
              revision_json.get<std::int64_t>();

          if (signed_revision < 0) {
            return false;
          }

          u.storage_projection_revision =
              static_cast<std::uint64_t>(
                  signed_revision
              );
        } catch (...) {
          return false;
        }
      } else {
        return false;
      }
    } else {
      u.storage_projection_revision = 0;
    }

    // Missing policy ID preserves backward compatibility. Malformed explicit
    // values fail closed instead of silently dropping an intended override.
    if (it.contains("placement_policy_id")) {
      if (!it["placement_policy_id"].is_string()) return false;

      u.placement_policy_id =
          it["placement_policy_id"].get<std::string>();

      if (!valid_user_placement_policy_id(
              u.placement_policy_id)) {
        return false;
      }
    } else {
      u.placement_policy_id = "";
    }

    by_fp_[u.fingerprint] = u;
  }

  return true;
}


//------------------------------------------------------------------------------
// Persistence: save() with atomic replace
//------------------------------------------------------------------------------

/*
UsersRegistry::save(path)
  - Writes a normalized snapshot of all users to disk.
  - Sorting keys produces stable output for diffing and reproducibility.
  - Uses atomic replace:
      write tmp in same directory -> rename tmp -> target

Caveats:
  - The JSON initializer currently includes {"address", u.address} twice.
    That is likely unintentional. Nlohmann JSON will keep the last duplicate key,
    but it’s confusing and should be removed.
*/
static std::size_t users_registry_existing_user_count_for_guard(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f.good()) return 0;

  try {
    json j;
    f >> j;

    if (j.is_object() && j.contains("users") && j["users"].is_array()) {
      return j["users"].size();
    }
  } catch (...) {
  }

  return 0;
}

bool UsersRegistry::save(
    const std::string& path
) const {
  std::lock_guard<std::mutex> lk(mu_);
  return save_locked(path);
}

bool UsersRegistry::save_locked(
    const std::string& path
) const {
  /*
   * Safety guard:
   *
   * users.json is security-critical. A valid but empty {"users":[]} file locks
   * every normal/admin user out of the server. If an existing registry on disk
   * contains users, never allow an accidental empty in-memory registry to replace
   * it through a generic save() call.
   *
   * A deliberate "delete all users" operation should use a separate explicit
   * maintenance path, not the normal registry save primitive.
   */
  if (by_fp_.empty()) {
    const std::filesystem::path target_path(path);
    const std::size_t existing_count =
        users_registry_existing_user_count_for_guard(target_path);

    if (existing_count > 0) {
      std::cerr
          << "[users] REFUSING to overwrite non-empty users registry with empty list: "
          << path
          << " existing_users="
          << existing_count
          << std::endl;
      return false;
    }
  }

  // Stable ordering makes the registry file deterministic and diff-friendly.
  std::vector<std::string> keys;
  keys.reserve(by_fp_.size());
  for (const auto& kv : by_fp_) keys.push_back(kv.first);
  std::sort(keys.begin(), keys.end());

  json j;
  j["users"] = json::array();
  for (const auto& k : keys) {
    const auto& u = by_fp_.at(k);

    // Security: never serialize path-, URL- or traversal-like placement data.
    // Invalid in-memory state must fail closed instead of being normalized into
    // an unintended local placement.
    if (!valid_user_home_node_id(u.home_node) ||
        !valid_user_placement_policy_id(
            u.placement_policy_id)) {
      return false;
    }

    json app_prefs = json::object();
    try {
      if (!u.app_prefs_json.empty()) {
        json parsed = json::parse(u.app_prefs_json);
        if (parsed.is_object()) app_prefs = parsed;
      }
    } catch (...) {
      app_prefs = json::object();
    }

    j["users"].push_back(json{
      // Existing
      {"fingerprint", u.fingerprint},
      {"name", u.name},
      {"role", u.role},
      {"status", u.status},
      {"added_at", u.added_at},
      {"last_seen", u.last_seen},
      {"notes", u.notes},

      // New: profile
      {"group", u.group},
      {"email", u.email},
      {"address", u.address},
      {"avatar_url", u.avatar_url},
      {"app_prefs", app_prefs},

      // Vault Master recovery is scoped to this exact user fingerprint.
      // Security: this contains public key material only.
      {"vault_master_recovery", json{
        {"status", norm_vault_master_recovery_status(u.vault_master_recovery_status)},
        {"public_key_b64", u.vault_master_recovery_public_key_b64},
        {"public_key_sha256", u.vault_master_recovery_public_key_sha256},
        {"created_at", u.vault_master_recovery_created_at < 0 ? 0 : u.vault_master_recovery_created_at},
        {"label", u.vault_master_recovery_label}
      }},

      // Cluster placement metadata. The policy is intent; home_node is
      // the currently realized placement.
      {"home_node", u.home_node},
      {"storage_projection_revision", u.storage_projection_revision},
      {"placement_policy_id", u.placement_policy_id},

      // New: storage metadata
      {"storage_state", norm_storage_state(u.storage_state)},
      {"quota_bytes", u.quota_bytes},
      {"root_rel", u.root_rel},
      {"storage_pool_id", u.storage_pool_id},
      {"storage_set_at", u.storage_set_at},
      {"storage_set_by", u.storage_set_by}
    });
  }

  std::filesystem::path p(path);
  std::error_code ec;

  // Ensure parent dir exists
  std::filesystem::create_directories(p.parent_path(), ec);
  if (ec) return false;

  // Unique temp file in the same directory (atomic rename guarantee)
  std::filesystem::path tmp = p;
  tmp += ".tmp.";
  tmp += std::to_string(::getpid());
  tmp += ".";
  tmp += std::to_string(static_cast<long long>(std::time(nullptr)));

  // Write temp
  {
    std::ofstream out(tmp.string(), std::ios::trunc);
    if (!out.good()) return false;
    out << j.dump(2) << "\n";
    out.flush();
    if (!out.good()) {
      std::filesystem::remove(tmp, ec);
      return false;
    }
  }

  // Atomic replace: rename tmp -> target
  ec.clear();
  std::filesystem::rename(tmp, p, ec);
  if (ec) {
    // Cleanup tmp on failure
    std::filesystem::remove(tmp, ec);
    return false;
  }

  return true;
}


//------------------------------------------------------------------------------
// Read helpers (thread-safe queries)
//------------------------------------------------------------------------------

/*
exists()
  - O(1) lookup by fingerprint.
*/
bool UsersRegistry::exists(const std::string& fp_hex) const {
  std::lock_guard<std::mutex> lk(mu_);
  return by_fp_.find(fp_hex) != by_fp_.end();
}

/*
get()
  - Returns a copy of the user record if present.
  - Copy is intentional: caller gets a stable snapshot without holding the lock.
*/
std::optional<UserRec> UsersRegistry::get(const std::string& fp_hex) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = by_fp_.find(fp_hex);
  if (it == by_fp_.end()) return std::nullopt;
  return it->second;
}

/*
Convenience authorization checks
  - These are intentionally strict: enabled must be set.
*/
bool UsersRegistry::is_enabled_user(const std::string& fp_hex) const {
  auto u = get(fp_hex);
  return u.has_value() && u->status == "enabled";
}

bool UsersRegistry::is_admin_enabled(const std::string& fp_hex) const {
  auto u = get(fp_hex);
  return u.has_value() && u->status == "enabled" && u->role == "admin";
}

std::string UsersRegistry::role_of(const std::string& fp_hex) const {
  auto u = get(fp_hex);
  return u.has_value() ? u->role : "";
}


//------------------------------------------------------------------------------
// Mutation helpers
//------------------------------------------------------------------------------

/*
ensure_present_disabled_user(fp, now_iso)
  - Used by "Approvals" onboarding flows:
      * when a fingerprint is first seen, create a disabled user record
      * admin later approves (enables) it
  - Returns true if created, false if already present.
*/
bool UsersRegistry::ensure_present_disabled_user(const std::string& fp_hex, const std::string& now_iso) {
  std::lock_guard<std::mutex> lk(mu_);
  if (by_fp_.find(fp_hex) != by_fp_.end()) return false;

  UserRec u;
  u.fingerprint = fp_hex;

  // Existing defaults
  u.name = "";
  u.role = "user";
  u.status = "disabled";      // created but not enabled
  u.added_at = now_iso;
  u.last_seen = "";
  u.notes = "";

  // New defaults
  u.group = "";
  u.email = "";
  u.address = "";
  u.avatar_url = "";
  u.app_prefs_json = "{}";

  // Storage defaults: user cannot use File Manager until allocated.
  u.storage_state = "unallocated";
  u.quota_bytes = 0;
  u.root_rel = "";
  u.storage_pool_id = ""; // default
  u.storage_set_at = "";
  u.storage_set_by = "";

  // Existing standalone behavior remains unchanged.
  u.home_node = "";
  u.placement_policy_id = "";

  by_fp_[fp_hex] = u;
  return true; // created
}

namespace {

// Compare only fields that are projected to a Storage Node.
//
// Security: this deliberately excludes role, profile data, Vault metadata,
// placement-policy intent and storage audit metadata. Those fields must not
// churn the authorization projection revision.
//
// Disabled and revoked are intentionally equivalent here: Storage receives
// only enabled=true/false.
bool storage_metadata_fields_equal(
    const UserRec& left,
    const UserRec& right
) {
  return
      left.storage_state == right.storage_state &&
      left.quota_bytes == right.quota_bytes &&
      left.root_rel == right.root_rel &&
      left.storage_pool_id == right.storage_pool_id &&
      left.storage_set_at == right.storage_set_at &&
      left.storage_set_by == right.storage_set_by;
}

bool storage_projection_semantics_equal(
    const UserRec& left,
    const UserRec& right
) {
  const bool left_enabled =
      left.status == "enabled";

  const bool right_enabled =
      right.status == "enabled";

  if (left_enabled != right_enabled) {
    return false;
  }

  const bool left_allocated =
      left.storage_state == "allocated";

  const bool right_allocated =
      right.storage_state == "allocated";

  if (left_allocated != right_allocated) {
    return false;
  }

  if (left.home_node != right.home_node) {
    return false;
  }

  if (!left_allocated) {
    // StorageUserProjection canonicalizes unallocated users to no quota/root/
    // pool metadata, so stale local storage metadata is not projection state.
    return true;
  }

  return
      left.quota_bytes == right.quota_bytes &&
      left.root_rel == right.root_rel &&
      left.storage_pool_id == right.storage_pool_id;
}

bool next_storage_projection_revision(
    std::uint64_t current,
    std::uint64_t* next
) {
  if (!next ||
      current ==
          std::numeric_limits<std::uint64_t>::max()) {
    return false;
  }

  *next = current + 1;

  // Revision zero is reserved for legacy/unversioned state.
  if (*next == 0) {
    return false;
  }

  return true;
}

} // namespace

/*
upsert()
  - Inserts or replaces a user record.
  - Applies normalization to prevent invalid values from entering the registry.
  - Caller must provide fingerprint.
*/
bool UsersRegistry::upsert(const UserRec& in) {
  // Security: generic profile updates must not bypass placement validation.
  if (!valid_user_home_node_id(in.home_node) ||
      !valid_user_placement_policy_id(
          in.placement_policy_id)) {
    return false;
  }

  std::lock_guard<std::mutex> lk(mu_);

  if (in.fingerprint.empty()) {
    return false;
  }

  UserRec u = in;

  // Normalize (schema firewall)
  u.role = norm_role(u.role);
  u.status = norm_status(u.status);
  u.storage_state =
      norm_storage_state(u.storage_state);

  if (u.app_prefs_json.empty()) {
    u.app_prefs_json = "{}";
  }

  const auto existing =
      by_fp_.find(u.fingerprint);

  if (existing != by_fp_.end()) {
    // Security: projection revisions are registry-owned. A generic caller
    // cannot forge, rewind or advance the Director's control-plane version.
    u.storage_projection_revision =
        existing->second.storage_projection_revision;
  } else {
    // Legacy/generic insertion remains unversioned until a write-through
    // projection mutation establishes the first authoritative revision.
    u.storage_projection_revision = 0;
  }

  by_fp_[u.fingerprint] =
      std::move(u);

  return true;
}

void UsersRegistry::set_storage_projection_commit_notifier(
    UsersRegistry::StorageProjectionCommitNotifier notifier
) {
  std::lock_guard<std::mutex> lk(mu_);
  storage_projection_commit_notifier_ =
      notifier;
}

UsersRegistry::StorageProjectionCommitNotifier
UsersRegistry::storage_projection_commit_notifier_copy_locked() const noexcept {
  try {
    return storage_projection_commit_notifier_;
  } catch (...) {
    // Security/correctness: failure to prepare a best-effort wake must never
    // change the result of an already-durable users.json commit.
    return {};
  }
}


bool UsersRegistry::upsert_persisted(
    const UserRec& in,
    const std::string& path,
    bool* projection_changed,
    std::string* error
) {
  if (projection_changed) {
    *projection_changed = false;
  }

  if (error) {
    error->clear();
  }

  if (path.empty()) {
    if (error) {
      *error = "users registry path is empty";
    }

    return false;
  }

  if (in.fingerprint.empty()) {
    if (error) {
      *error = "fingerprint is empty";
    }

    return false;
  }

  if (!valid_user_home_node_id(in.home_node) ||
      !valid_user_placement_policy_id(
          in.placement_policy_id)) {
    if (error) {
      *error = "invalid user placement metadata";
    }

    return false;
  }

  UserRec candidate = in;
  bool projection_differs = false;

  candidate.role =
      norm_role(candidate.role);

  candidate.status =
      norm_status(candidate.status);

  candidate.storage_state =
      norm_storage_state(
          candidate.storage_state
      );

  if (candidate.app_prefs_json.empty()) {
    candidate.app_prefs_json = "{}";
  }

  std::unique_lock<std::mutex> lk(mu_);

  const auto existing =
      by_fp_.find(candidate.fingerprint);

  const bool inserting_new =
      existing == by_fp_.end();

  UserRec previous;

  if (!inserting_new) {
    previous =
        existing->second;

    projection_differs =
        !storage_projection_semantics_equal(
            previous,
            candidate
        );

    candidate.storage_projection_revision =
        previous.storage_projection_revision;

    if (projection_differs) {
      std::uint64_t next_revision = 0;

      if (!next_storage_projection_revision(
              previous.storage_projection_revision,
              &next_revision)) {
        if (error) {
          *error =
              "storage projection revision exhausted";
        }

        return false;
      }

      candidate.storage_projection_revision =
          next_revision;

      if (projection_changed) {
        *projection_changed = true;
      }
    }
  } else {
    projection_differs = true;

    // Creation is the first authoritative persisted projection snapshot.
    // Starting at 1 also means a newly created + immediately placed user can
    // be synchronized without inventing a revision outside UsersRegistry.
    candidate.storage_projection_revision = 1;

    if (projection_changed) {
      *projection_changed = true;
    }
  }

  by_fp_[candidate.fingerprint] =
      candidate;

  // Security: projection data and its revision are committed by the same
  // users.json replacement. On failure both are rolled back together.
  if (!save_locked(path)) {
    if (inserting_new) {
      by_fp_.erase(
          candidate.fingerprint
      );
    } else {
      by_fp_[candidate.fingerprint] =
          std::move(previous);
    }

    if (projection_changed) {
      *projection_changed = false;
    }

    if (error) {
      *error =
          "users registry persistence failed";
    }

    return false;
  }

  const auto projection_notifier =
      projection_differs
          ? storage_projection_commit_notifier_copy_locked()
          : StorageProjectionCommitNotifier{};

  // Security: never invoke external callbacks while holding the registry
  // mutation lock. The durable commit above is authoritative even if wake fails.
  lk.unlock();

  if (projection_notifier) {
    try {
      projection_notifier();
    } catch (...) {
      // Best-effort latency optimization. Periodic reconciliation remains
      // the safety net and the successful durable commit stays successful.
    }
  }

  return true;
}

/*
set_status(), set_role()
  - Small mutation helpers used by admin operations.
  - Values are normalized to avoid invalid states.
*/
bool UsersRegistry::set_status(const std::string& fp_hex, const std::string& status) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = by_fp_.find(fp_hex);
  if (it == by_fp_.end()) return false;
  it->second.status = norm_status(status);
  return true;
}


bool UsersRegistry::set_status_persisted(
    const std::string& fp_hex,
    const std::string& status,
    const std::string& path,
    bool* changed,
    std::string* error
) {
  if (changed) {
    *changed = false;
  }

  if (error) {
    error->clear();
  }

  if (fp_hex.empty()) {
    if (error) {
      *error = "fingerprint is empty";
    }

    return false;
  }

  if (path.empty()) {
    if (error) {
      *error = "users registry path is empty";
    }

    return false;
  }

  const std::string normalized_status =
      norm_status(status);

  std::unique_lock<std::mutex> lk(mu_);

  auto iterator =
      by_fp_.find(fp_hex);

  if (iterator == by_fp_.end()) {
    if (error) {
      *error = "user not found";
    }

    return false;
  }

  const UserRec previous =
      iterator->second;

  if (previous.status ==
      normalized_status) {
    return true;
  }

  UserRec candidate =
      previous;

  candidate.status =
      normalized_status;

  const bool projection_differs =
      !storage_projection_semantics_equal(
          previous,
          candidate
      );

  if (projection_differs) {
    std::uint64_t next_revision = 0;

    if (!next_storage_projection_revision(
            previous.storage_projection_revision,
            &next_revision)) {
      if (error) {
        *error =
            "storage projection revision exhausted";
      }

      return false;
    }

    candidate.storage_projection_revision =
        next_revision;
  }

  iterator->second =
      candidate;

  if (!save_locked(path)) {
    iterator->second =
        previous;

    if (error) {
      *error =
          "users registry persistence failed";
    }

    return false;
  }

  if (changed) {
    *changed = true;
  }

  const auto projection_notifier =
      projection_differs
          ? storage_projection_commit_notifier_copy_locked()
          : StorageProjectionCommitNotifier{};

  lk.unlock();

  if (projection_notifier) {
    try {
      projection_notifier();
    } catch (...) {
      // Best-effort wake only; durable registry state remains authoritative.
    }
  }

  return true;
}

bool UsersRegistry::set_storage_metadata_persisted(
    const std::string& fp_hex,
    const UserStorageMetadataUpdate& update,
    const std::string& path,
    bool* changed,
    bool* projection_changed,
    std::string* error
) {
  if (changed) {
    *changed = false;
  }

  if (projection_changed) {
    *projection_changed = false;
  }

  if (error) {
    error->clear();
  }

  if (fp_hex.empty()) {
    if (error) {
      *error = "fingerprint is empty";
    }

    return false;
  }

  if (path.empty()) {
    if (error) {
      *error = "users registry path is empty";
    }

    return false;
  }

  std::unique_lock<std::mutex> lk(mu_);

  auto iterator =
      by_fp_.find(fp_hex);

  if (iterator == by_fp_.end()) {
    if (error) {
      *error = "user not found";
    }

    return false;
  }

  const UserRec previous =
      iterator->second;

  UserRec candidate =
      previous;

  candidate.storage_state =
      norm_storage_state(
          update.storage_state
      );

  candidate.quota_bytes =
      update.quota_bytes;

  candidate.root_rel =
      update.root_rel;

  candidate.storage_pool_id =
      update.storage_pool_id;

  candidate.storage_set_at =
      update.storage_set_at;

  candidate.storage_set_by =
      update.storage_set_by;

  if (storage_metadata_fields_equal(
          previous,
          candidate)) {
    return true;
  }

  const bool projection_differs =
      !storage_projection_semantics_equal(
          previous,
          candidate
      );

  if (projection_differs) {
    std::uint64_t next_revision = 0;

    if (!next_storage_projection_revision(
            previous.storage_projection_revision,
            &next_revision)) {
      if (error) {
        *error =
            "storage projection revision exhausted";
      }

      return false;
    }

    candidate.storage_projection_revision =
        next_revision;
  }

  iterator->second =
      candidate;

  // Security: storage metadata and the revision describing its Node-visible
  // projection are committed by the same users.json replacement.
  if (!save_locked(path)) {
    iterator->second =
        previous;

    if (error) {
      *error =
          "users registry persistence failed";
    }

    return false;
  }

  if (changed) {
    *changed = true;
  }

  if (projection_changed) {
    *projection_changed =
        projection_differs;
  }

  const auto projection_notifier =
      projection_differs
          ? storage_projection_commit_notifier_copy_locked()
          : StorageProjectionCommitNotifier{};

  // Security: callback runs only after the durable commit and outside mu_.
  lk.unlock();

  if (projection_notifier) {
    try {
      projection_notifier();
    } catch (...) {
      // Best-effort wake only; periodic reconciliation remains authoritative.
    }
  }

  return true;
}

bool UsersRegistry::switch_storage_mapping_persisted_if_pool_current(
    const std::string& fp_hex,
    const std::string& expected_storage_pool_id,
    const std::string& new_storage_pool_id,
    const std::string& new_root_rel,
    const std::string& storage_set_at,
    const std::string& storage_set_by,
    const std::string& path,
    bool* expectation_matched,
    bool* changed,
    bool* projection_changed,
    std::string* error
) {
  if (expectation_matched) {
    *expectation_matched = false;
  }

  if (changed) {
    *changed = false;
  }

  if (projection_changed) {
    *projection_changed = false;
  }

  if (error) {
    error->clear();
  }

  if (fp_hex.empty()) {
    if (error) {
      *error = "fingerprint is empty";
    }

    return false;
  }

  if (path.empty()) {
    if (error) {
      *error = "users registry path is empty";
    }

    return false;
  }

  std::unique_lock<std::mutex> lk(mu_);

  auto iterator =
      by_fp_.find(fp_hex);

  if (iterator == by_fp_.end()) {
    if (error) {
      *error = "user not found";
    }

    return false;
  }

  // Security: compare while holding the same lock used for the mapping
  // mutation and persistence. A migration planned against an older pool
  // cannot overwrite a newer pool selection.
  if (iterator->second.storage_pool_id !=
      expected_storage_pool_id) {
    return true;
  }

  if (expectation_matched) {
    *expectation_matched = true;
  }

  const UserRec previous =
      iterator->second;

  UserRec candidate =
      previous;

  // Security: migration owns only mapping/audit fields. In particular, keep
  // current quota and allocation state from the locked record instead of
  // copying them from a potentially stale worker snapshot.
  candidate.storage_pool_id =
      new_storage_pool_id;

  candidate.root_rel =
      new_root_rel;

  candidate.storage_set_at =
      storage_set_at;

  candidate.storage_set_by =
      storage_set_by;

  if (storage_metadata_fields_equal(
          previous,
          candidate)) {
    return true;
  }

  const bool projection_differs =
      !storage_projection_semantics_equal(
          previous,
          candidate
      );

  if (projection_differs) {
    std::uint64_t next_revision = 0;

    if (!next_storage_projection_revision(
            previous.storage_projection_revision,
            &next_revision)) {
      if (error) {
        *error =
            "storage projection revision exhausted";
      }

      return false;
    }

    candidate.storage_projection_revision =
        next_revision;
  }

  iterator->second =
      candidate;

  // Security: CAS comparison, mapping, projection revision, persistence and
  // rollback are one locked transaction from the registry's point of view.
  if (!save_locked(path)) {
    iterator->second =
        previous;

    if (changed) {
      *changed = false;
    }

    if (projection_changed) {
      *projection_changed = false;
    }

    if (error) {
      *error =
          "users registry persistence failed";
    }

    return false;
  }

  if (changed) {
    *changed = true;
  }

  if (projection_changed) {
    *projection_changed =
        projection_differs;
  }

  const auto projection_notifier =
      projection_differs
          ? storage_projection_commit_notifier_copy_locked()
          : StorageProjectionCommitNotifier{};

  // Security: stale CAS/no-op paths never reach this notification point.
  lk.unlock();

  if (projection_notifier) {
    try {
      projection_notifier();
    } catch (...) {
      // Best-effort wake only; durable mapping remains authoritative.
    }
  }

  return true;
}

bool UsersRegistry::set_role(const std::string& fp_hex, const std::string& role) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = by_fp_.find(fp_hex);
  if (it == by_fp_.end()) return false;
  it->second.role = norm_role(role);
  return true;
}

/*
set_name_notes()
  - Updates UI-facing metadata fields.
  - Does not validate content here; caller should enforce length/charset policies
    if needed.
*/
bool UsersRegistry::set_name_notes(const std::string& fp_hex, const std::string& name, const std::string& notes) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = by_fp_.find(fp_hex);
  if (it == by_fp_.end()) return false;
  it->second.name = name;
  it->second.notes = notes;
  return true;
}

/*
set_home_node()
  - Updates cluster placement metadata in memory.
  - The caller remains responsible for persistence and NodeRegistry policy.
*/
bool UsersRegistry::set_home_node(
    const std::string& fp_hex,
    const std::string& home_node
) {
  if (!valid_user_home_node_id(home_node)) {
    return false;
  }

  std::lock_guard<std::mutex> lk(mu_);

  auto iterator = by_fp_.find(fp_hex);
  if (iterator == by_fp_.end()) {
    return false;
  }

  iterator->second.home_node = home_node;
  return true;
}

bool UsersRegistry::set_home_node_persisted(
    const std::string& fp_hex,
    const std::string& home_node,
    const std::string& path,
    bool* changed,
    std::string* error
) {
  if (changed) {
    *changed = false;
  }

  if (error) {
    error->clear();
  }

  if (fp_hex.empty()) {
    if (error) *error = "fingerprint is empty";
    return false;
  }

  if (home_node.empty() ||
      !valid_user_home_node_id(home_node)) {
    if (error) *error = "invalid home node ID";
    return false;
  }

  if (path.empty()) {
    if (error) *error = "users registry path is empty";
    return false;
  }

  std::unique_lock<std::mutex> lk(mu_);

  auto iterator = by_fp_.find(fp_hex);

  if (iterator == by_fp_.end()) {
    if (error) *error = "user not found";
    return false;
  }

  if (iterator->second.home_node == home_node) {
    return true;
  }

  const UserRec previous =
      iterator->second;

  std::uint64_t next_revision = 0;

  if (!next_storage_projection_revision(
          previous.storage_projection_revision,
          &next_revision)) {
    if (error) {
      *error =
          "storage projection revision exhausted";
    }

    return false;
  }

  iterator->second.home_node =
      home_node;

  iterator->second.storage_projection_revision =
      next_revision;

  // Security: placement and its projection revision are persisted together.
  // Save failure restores both values under the same mutation lock.
  if (!save_locked(path)) {
    iterator->second =
        previous;

    if (error) {
      *error = "users registry persistence failed";
    }

    return false;
  }

  if (changed) {
    *changed = true;
  }

  const auto projection_notifier =
      storage_projection_commit_notifier_copy_locked();

  // Security: placement is durable before reconciliation is signalled.
  lk.unlock();

  if (projection_notifier) {
    try {
      projection_notifier();
    } catch (...) {
      // Best-effort wake only; durable placement remains authoritative.
    }
  }

  return true;
}


bool UsersRegistry::set_home_node_persisted_if_current(
    const std::string& fp_hex,
    const std::string& expected_home_node,
    const std::string& expected_placement_policy_id,
    const std::string& new_home_node,
    const std::string& path,
    bool* changed,
    bool* expectation_matched,
    std::string* error
) {
  if (changed) {
    *changed = false;
  }

  if (expectation_matched) {
    *expectation_matched = false;
  }

  if (error) {
    error->clear();
  }

  if (fp_hex.empty()) {
    if (error) *error = "fingerprint is empty";
    return false;
  }

  // Empty expected home_node is valid and represents legacy/local placement.
  if (!valid_user_home_node_id(
          expected_home_node)) {
    if (error) *error = "invalid expected home node ID";
    return false;
  }

  // Empty expected policy ID is valid and represents no policy override.
  if (!valid_user_placement_policy_id(
          expected_placement_policy_id)) {
    if (error) {
      *error =
          "invalid expected placement policy ID";
    }

    return false;
  }

  if (new_home_node.empty() ||
      !valid_user_home_node_id(
          new_home_node)) {
    if (error) *error = "invalid home node ID";
    return false;
  }

  if (path.empty()) {
    if (error) *error = "users registry path is empty";
    return false;
  }

  std::unique_lock<std::mutex> lk(mu_);

  auto iterator = by_fp_.find(fp_hex);

  if (iterator == by_fp_.end()) {
    if (error) *error = "user not found";
    return false;
  }

  // TOCTOU protection: compare both preview-relevant user fields while
  // holding the same registry lock used for mutation and persistence.
  if (iterator->second.home_node !=
          expected_home_node ||
      iterator->second.placement_policy_id !=
          expected_placement_policy_id) {
    return true;
  }

  if (expectation_matched) {
    *expectation_matched = true;
  }

  if (iterator->second.home_node ==
      new_home_node) {
    return true;
  }

  const UserRec previous =
      iterator->second;

  std::uint64_t next_revision = 0;

  if (!next_storage_projection_revision(
          previous.storage_projection_revision,
          &next_revision)) {
    if (error) {
      *error =
          "storage projection revision exhausted";
    }

    return false;
  }

  iterator->second.home_node =
      new_home_node;

  iterator->second.storage_projection_revision =
      next_revision;

  // Security: compare, placement, revision, persistence and rollback stay
  // under one registry lock so stale workers cannot split authorization state.
  if (!save_locked(path)) {
    iterator->second =
        previous;

    if (error) {
      *error =
          "users registry persistence failed";
    }

    return false;
  }

  if (changed) {
    *changed = true;
  }

  const auto projection_notifier =
      storage_projection_commit_notifier_copy_locked();

  // Security: CAS state and revision are durable before reconciliation wakes.
  lk.unlock();

  if (projection_notifier) {
    try {
      projection_notifier();
    } catch (...) {
      // Best-effort wake only; durable placement remains authoritative.
    }
  }

  return true;
}

bool UsersRegistry::set_placement_policy_id_persisted(
    const std::string& fp_hex,
    const std::string& placement_policy_id,
    const std::string& path,
    bool* changed,
    std::string* error
) {
  if (changed) {
    *changed = false;
  }

  if (error) {
    error->clear();
  }

  if (fp_hex.empty()) {
    if (error) *error = "fingerprint is empty";
    return false;
  }

  // Empty is valid and explicitly clears the user's policy override.
  if (!valid_user_placement_policy_id(
          placement_policy_id)) {
    if (error) *error = "invalid placement policy ID";
    return false;
  }

  if (path.empty()) {
    if (error) *error = "users registry path is empty";
    return false;
  }

  std::lock_guard<std::mutex> lk(mu_);

  auto iterator = by_fp_.find(fp_hex);

  if (iterator == by_fp_.end()) {
    if (error) *error = "user not found";
    return false;
  }

  if (iterator->second.placement_policy_id ==
      placement_policy_id) {
    return true;
  }

  const std::string previous_policy_id =
      iterator->second.placement_policy_id;

  iterator->second.placement_policy_id =
      placement_policy_id;

  // Security: persist while holding the registry mutation lock. A failed
  // atomic replacement restores the previous in-memory policy reference.
  if (!save_locked(path)) {
    iterator->second.placement_policy_id =
        previous_policy_id;

    if (error) {
      *error = "users registry persistence failed";
    }

    return false;
  }

  if (changed) {
    *changed = true;
  }

  return true;
}


/*
touch_last_seen()
  - Updates last_seen timestamp for activity tracking / UI.
  - Caller should pass an ISO8601 string consistent with the rest of PQ-NAS.
*/
bool UsersRegistry::touch_last_seen(const std::string& fp_hex, const std::string& now_iso) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = by_fp_.find(fp_hex);
  if (it == by_fp_.end()) return false;
  it->second.last_seen = now_iso;
  return true;
}

/*
erase()
  - Removes a user record entirely.
  - Higher layers should ensure policy: do not allow deleting the last admin, etc.
*/
bool UsersRegistry::erase(const std::string& fp_hex) {
  std::lock_guard<std::mutex> lk(mu_);
  return by_fp_.erase(fp_hex) > 0;
}


bool UsersRegistry::erase_persisted_if_unplaced(
    const std::string& fp_hex,
    const std::string& path,
    bool* erased,
    bool* placement_blocked,
    std::string* error
) {
  if (erased) {
    *erased = false;
  }

  if (placement_blocked) {
    *placement_blocked = false;
  }

  if (error) {
    error->clear();
  }

  if (fp_hex.empty()) {
    if (error) {
      *error = "fingerprint is empty";
    }

    return false;
  }

  if (path.empty()) {
    if (error) {
      *error = "users registry path is empty";
    }

    return false;
  }

  std::lock_guard<std::mutex> lk(mu_);

  auto iterator =
      by_fp_.find(fp_hex);

  if (iterator == by_fp_.end()) {
    if (error) {
      *error = "user not found";
    }

    return false;
  }

  // Security: deleting a placed Director user before its Storage Node has
  // received a newer revoke/tombstone could leave stale authorization active.
  if (!iterator->second.home_node.empty()) {
    if (placement_blocked) {
      *placement_blocked = true;
    }

    return true;
  }

  const UserRec previous =
      iterator->second;

  by_fp_.erase(iterator);

  // Security/correctness: memory and users.json form one logical mutation.
  // Restore the complete record if persistence fails.
  if (!save_locked(path)) {
    by_fp_[previous.fingerprint] =
        previous;

    if (error) {
      *error =
          "users registry persistence failed";
    }

    return false;
  }

  if (erased) {
    *erased = true;
  }

  return true;
}

/*
snapshot()
  - Returns a full copy of the internal map.
  - Useful for pages that need to list/iterate users without holding mu_.
*/
std::unordered_map<std::string, UserRec> UsersRegistry::snapshot() const {
  std::lock_guard<std::mutex> lk(mu_);
  return by_fp_;
}

std::unordered_map<
    std::string,
    std::size_t
> UsersRegistry::home_node_assignment_counts() const {
  std::lock_guard<std::mutex> lk(mu_);

  std::unordered_map<
      std::string,
      std::size_t
  > counts;

  for (const auto& entry : by_fp_) {
    const std::string& home_node =
        entry.second.home_node;

    if (!home_node.empty()) {
      ++counts[home_node];
    }
  }

  return counts;
}

std::unordered_map<
    std::string,
    std::size_t
> UsersRegistry::placement_policy_assignment_counts() const {
  std::lock_guard<std::mutex> lk(mu_);

  std::unordered_map<
      std::string,
      std::size_t
  > counts;

  for (const auto& entry : by_fp_) {
    const std::string& policy_id =
        entry.second.placement_policy_id;

    // Security: expose only aggregate references to explicit policy IDs.
    // Empty values mean no override and must not create a pseudo-policy.
    if (!policy_id.empty()) {
      ++counts[policy_id];
    }
  }

  return counts;
}

} // namespace pqnas