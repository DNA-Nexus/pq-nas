# DNA-Nexus / PQ-NAS App Data Storage Map

Status: draft
Recommended path: `docs/technical/app-data-storage-map.md`

This document maps where DNA-Nexus / PQ-NAS apps and core features store their persistent data.

The goal is to make the system easier to maintain, back up, migrate, audit, and debug. It also helps future app development follow the same storage model instead of creating hidden or duplicated data islands.

## Scope

This document covers:

* user and workspace file payloads
* logical file metadata
* app-specific metadata
* SQLite databases
* JSON configuration files
* internal payload areas such as Trash and file versions
* known app-specific persistence layers

This document does not yet cover every temporary cache, thumbnail cache, browser-side state, or install-time package file.

## Runtime base directories

Typical runtime locations currently use the `pqnas` path name for compatibility.

| Path                           | Purpose                                                              |
| ------------------------------ | -------------------------------------------------------------------- |
| `/srv/pqnas/config`            | Server configuration, JSON state, and many SQLite metadata databases |
| `/srv/pqnas/data`              | User data, workspace data, avatars, internal data areas              |
| `/srv/pqnas/data/users`        | User file roots                                                      |
| `/srv/pqnas/data/workspaces`   | Workspace file roots                                                 |
| `/srv/pqnas/data/avatars`      | User avatar payloads                                                 |
| `/srv/pqnas/data/.pqnas/trash` | Internal Trash payload area                                          |
| `/srv/pqnas/.snapshots`        | Snapshot-related storage area                                        |
| `/srv/pqnas/circlestack.db`    | Circle Stack main database                                           |

Some internal paths, database names, or service names may still use `pqnas` even when the public product name is DNA-Nexus Server.

## Core storage model

DNA-Nexus / PQ-NAS is moving away from a simple “filesystem is the only source of truth” model.

The visible file namespace is logical. A user-visible path may be mapped to a physical path through metadata.

Core idea:

| Concept       | Meaning                                                                    |
| ------------- | -------------------------------------------------------------------------- |
| Logical path  | The path visible to the API and UI                                         |
| Physical path | The actual on-disk location of the payload                                 |
| Storage pool  | The pool where the payload currently resides                               |
| Tier state    | Whether the file is in landing, migrating, capacity, or another tier state |
| Scope         | Usually `user` or `workspace`                                              |
| Scope id      | User fingerprint or workspace id                                           |

The main metadata authority for live files is the file location index.

Expected database:

| Database                            | Purpose                                       |
| ----------------------------------- | --------------------------------------------- |
| `/srv/pqnas/config/storage_meta.db` | Logical file path to physical storage mapping |

Main table:

| Table            | Purpose                                                       |
| ---------------- | ------------------------------------------------------------- |
| `file_locations` | Maps visible logical file paths to physical storage locations |

Important fields:

| Field              | Meaning                           |
| ------------------ | --------------------------------- |
| `fp`               | User fingerprint namespace        |
| `logical_rel_path` | Canonical logical file path       |
| `current_pool`     | Current storage pool id           |
| `physical_path`    | Current on-disk file path         |
| `tier_state`       | Current tiering state             |
| `size_bytes`       | Last known file size              |
| `mtime_epoch`      | Last known file modification time |
| `created_epoch`    | Metadata creation time            |
| `updated_epoch`    | Last metadata update time         |
| `version`          | Monotonic metadata version        |

Directories may be implicit. For example, if rows exist for `docs/a.txt` and `docs/sub/b.txt`, the logical directories `docs` and `docs/sub` can be inferred even if no explicit directory row exists.

## File Manager

File Manager primarily works on the logical file namespace.

| Data                       | Storage                                                    |
| -------------------------- | ---------------------------------------------------------- |
| Live file payloads         | User/workspace storage roots and storage pools             |
| Logical path metadata      | `file_locations`                                           |
| Physical location metadata | `file_locations.physical_path`                             |
| Pool/tier metadata         | `file_locations.current_pool`, `file_locations.tier_state` |
| Size and mtime metadata    | `file_locations.size_bytes`, `file_locations.mtime_epoch`  |

File Manager should treat logical metadata as authoritative when a `file_locations` row exists.

Legacy physical fallback may still exist for older installs or restored files, but new code should prefer the logical metadata layer.

## Gallery albums

Gallery albums store references to files. They should not duplicate photo or video payloads.

Expected database:

| Database                            | Purpose                             |
| ----------------------------------- | ----------------------------------- |
| `/srv/pqnas/config/gallery_meta.db` | Gallery metadata and album metadata |

Main tables:

| Table                 | Purpose                  |
| --------------------- | ------------------------ |
| `gallery_albums`      | Album records            |
| `gallery_album_items` | Files included in albums |

`gallery_albums` stores:

| Field                    | Meaning                                   |
| ------------------------ | ----------------------------------------- |
| `album_id`               | Stable album id                           |
| `scope_type`             | `user` or `workspace`                     |
| `scope_id`               | User fingerprint or workspace id          |
| `name`                   | Album name                                |
| `description`            | Album description                         |
| `cover_logical_rel_path` | Optional logical path used as album cover |
| `created_epoch`          | Created timestamp                         |
| `updated_epoch`          | Updated timestamp                         |

`gallery_album_items` stores:

| Field              | Meaning                                        |
| ------------------ | ---------------------------------------------- |
| `album_id`         | Parent album id                                |
| `scope_type`       | `user` or `workspace`                          |
| `scope_id`         | User fingerprint or workspace id               |
| `logical_rel_path` | Logical path to the file included in the album |
| `added_epoch`      | Added timestamp                                |
| `sort_order`       | Album ordering hint                            |

Important behavior:

* Album items point to logical file paths.
* The original files remain in normal user/workspace storage.
* When files or folders are moved, album item paths should be rewritten to keep albums valid.
* Album covers also use logical paths.

## Gallery item metadata

Gallery metadata stores user-visible annotations for media items.

Expected database:

| Database                            | Purpose               |
| ----------------------------------- | --------------------- |
| `/srv/pqnas/config/gallery_meta.db` | Gallery item metadata |

Main table:

| Table          | Purpose                   |
| -------------- | ------------------------- |
| `gallery_meta` | Per-file gallery metadata |

Important fields:

| Field              | Meaning                                   |
| ------------------ | ----------------------------------------- |
| `scope_type`       | `user` or `workspace`                     |
| `scope_id`         | User fingerprint or workspace id          |
| `logical_rel_path` | Logical path inside the scope             |
| `item_type`        | File type marker, currently mainly `file` |
| `rating`           | Rating from 0 to 5                        |
| `tags_text`        | Freeform tags                             |
| `notes_text`       | Notes, caption, or description            |
| `size_bytes`       | Cached file size                          |
| `mtime_epoch`      | Cached file mtime                         |
| `created_epoch`    | Metadata creation time                    |
| `updated_epoch`    | Metadata update time                      |

Gallery metadata should follow file moves and deletes using logical path update/delete helpers.

## File annotations

File annotations are general file notes, separate from Gallery-specific metadata.

Database path: injected by server startup wiring.

Main table:

| Table        | Purpose                                         |
| ------------ | ----------------------------------------------- |
| `file_notes` | General notes/descriptions for files or folders |

Important fields:

| Field              | Meaning                                           |
| ------------------ | ------------------------------------------------- |
| `scope_type`       | `user` or `workspace`                             |
| `scope_id`         | User fingerprint or workspace id                  |
| `logical_rel_path` | Logical file or folder path                       |
| `item_kind`        | File/folder/type marker                           |
| `description`      | User-visible note or description                  |
| `updated_by_fp`    | Fingerprint of the user who last updated the note |
| `created_at_epoch` | Created timestamp                                 |
| `updated_at_epoch` | Updated timestamp                                 |

## File locks

File locks are stored separately from file payloads and file annotations.

Database path: injected by server startup wiring.

Main table:

| Table        | Purpose                                |
| ------------ | -------------------------------------- |
| `file_locks` | Live lock records for files or folders |

Important fields:

| Field              | Meaning                          |
| ------------------ | -------------------------------- |
| `scope_type`       | `user` or `workspace`            |
| `scope_id`         | User fingerprint or workspace id |
| `logical_rel_path` | Locked logical path              |
| `item_kind`        | File/folder/type marker          |
| `locked_by_fp`     | Fingerprint of locking user      |
| `note`             | Optional lock note               |
| `created_at_epoch` | Created timestamp                |
| `updated_at_epoch` | Updated timestamp                |
| `expires_at_epoch` | Optional expiration timestamp    |

Lock conflict checks should account for subtree conflicts. A lock on a folder may conflict with operations on children below that folder.

## File versions

File versions preserve previous file payloads during overwrite, restore-over-existing, and delete-preserve flows.

Expected database:

| Database                             | Purpose               |
| ------------------------------------ | --------------------- |
| `/srv/pqnas/config/file_versions.db` | File version metadata |

Version payloads are stored below the relevant user or workspace root:

```
<scope_root>/.pqnas/versions/blobs/<shard>/<version_id>.bin
```

Main tables:

| Table                | Purpose                          |
| -------------------- | -------------------------------- |
| `file_versions`      | Preserved version metadata       |
| `file_version_flags` | Notes/flags attached to versions |

Important `file_versions` fields:

| Field                  | Meaning                                      |
| ---------------------- | -------------------------------------------- |
| `version_id`           | Stable version id                            |
| `scope_type`           | `user` or `workspace`                        |
| `scope_id`             | User fingerprint or workspace id             |
| `logical_rel_path`     | Logical path of the original file            |
| `event_kind`           | Version event type                           |
| `created_at`           | Human-readable timestamp                     |
| `created_epoch`        | Epoch timestamp                              |
| `actor_fp`             | Actor fingerprint                            |
| `actor_name_snapshot`  | Actor display name snapshot                  |
| `bytes`                | Preserved blob size                          |
| `sha256_hex`           | Preserved blob hash                          |
| `source_physical_path` | Source physical path at preservation time    |
| `blob_rel_path`        | Blob path relative to the scope root         |
| `is_deleted_event`     | Whether this version came from a delete flow |

Known event kinds include:

* `overwrite_preserve`
* `restore_preserve`
* `delete_preserve`

File versions are full preserved copies, not binary diffs. Large files can therefore create large version storage.

## Trash

Trash separates payload storage from trash metadata.

Expected database:

| Database                     | Purpose        |
| ---------------------------- | -------------- |
| `/srv/pqnas/config/trash.db` | Trash metadata |

Payload area:

```
/srv/pqnas/data/.pqnas/trash/...
```

Main table:

| Table         | Purpose                        |
| ------------- | ------------------------------ |
| `trash_items` | One row per logical trash item |

Important fields:

| Field                   | Meaning                                     |
| ----------------------- | ------------------------------------------- |
| `trash_id`              | Stable trash item id                        |
| `scope_type`            | `user` or `workspace`                       |
| `scope_id`              | User fingerprint or workspace id            |
| `deleted_by_fp`         | User who moved the item to Trash            |
| `origin_app`            | App or feature that created the trash event |
| `item_type`             | File or folder marker                       |
| `original_rel_path`     | Original logical path                       |
| `storage_root`          | Original storage root                       |
| `trash_rel_path`        | Relative path inside Trash                  |
| `payload_physical_path` | Actual trash payload path                   |
| `source_pool`           | Original pool hint                          |
| `source_tier_state`     | Original tier hint                          |
| `size_bytes`            | Payload size                                |
| `file_count`            | File count for folder trash entries         |
| `deleted_epoch`         | Deletion timestamp                          |
| `purge_after_epoch`     | Automatic purge deadline                    |
| `restore_status`        | Trash lifecycle state                       |
| `status_updated_epoch`  | Last status update timestamp                |

Typical lifecycle:

* `trashed`
* `restoring`
* `restored`
* `purging`
* `purged`

Trash is recoverable for a limited time, but active Trash should count toward quota.

## Drop Zone

Drop Zone stores public upload link configuration and upload records in SQLite. Uploaded files are stored in the owner-selected destination path.

Database path: injected by server startup wiring.

Main tables:

| Table               | Purpose                     |
| ------------------- | --------------------------- |
| `drop_zones`        | Public upload link metadata |
| `drop_zone_uploads` | Completed upload records    |

Important `drop_zones` fields:

| Field              | Meaning                                       |
| ------------------ | --------------------------------------------- |
| `id`               | Drop Zone id                                  |
| `token_hash`       | Hash of public token; raw token is not stored |
| `public_path`      | Optional public-facing path                   |
| `owner_fp`         | Owner fingerprint                             |
| `name`             | Drop Zone name                                |
| `destination_path` | Destination logical path                      |
| `password_hash`    | Optional password hash                        |
| `created_epoch`    | Created timestamp                             |
| `expires_epoch`    | Expiry timestamp                              |
| `last_used_epoch`  | Last upload timestamp                         |
| `max_file_bytes`   | Per-file size limit                           |
| `max_total_bytes`  | Total upload limit                            |
| `bytes_uploaded`   | Denormalized uploaded byte counter            |
| `upload_count`     | Denormalized upload counter                   |
| `branding_json`    | Branded Drop Zone page settings               |
| `duplicate_policy` | Filename collision policy                     |
| `disabled`         | Whether the link is disabled                  |

Important `drop_zone_uploads` fields:

| Field               | Meaning                         |
| ------------------- | ------------------------------- |
| `id`                | Upload record id                |
| `drop_zone_id`      | Parent Drop Zone id             |
| `original_filename` | Filename supplied by uploader   |
| `stored_filename`   | Final stored filename           |
| `stored_path`       | Internal stored path            |
| `size_bytes`        | Uploaded file size              |
| `sha256`            | Uploaded file hash              |
| `uploader_name`     | Optional uploader name          |
| `uploader_message`  | Optional uploader message       |
| `remote_ip`         | Remote IP address               |
| `user_agent`        | User agent                      |
| `created_epoch`     | Upload timestamp                |
| `scan_status`       | Malware scan status placeholder |

Security note:

* The raw public token should not be persisted.
* Public routes must not expose `stored_path`.
* Public responses should only expose safe projections such as filename, size, timestamp, and optional uploader name/message.

## Echo Stack

Echo Stack stores bookmark/link metadata separately from archived or indexed content.

Database path: injected by server startup wiring.

Main tables:

| Table                | Purpose                                   |
| -------------------- | ----------------------------------------- |
| `echo_stack_items`   | Bookmark and archive metadata             |
| `echo_stack_content` | Searchable extracted/indexed page content |

Important `echo_stack_items` fields:

| Field               | Meaning                      |
| ------------------- | ---------------------------- |
| `id`                | Item id                      |
| `owner_fp`          | Owner fingerprint            |
| `url`               | Original URL                 |
| `final_url`         | Final resolved URL           |
| `title`             | Page title                   |
| `description`       | Page description             |
| `site_name`         | Site name                    |
| `favicon_url`       | Favicon URL                  |
| `preview_image_url` | Preview image URL            |
| `tags_text`         | Tags                         |
| `collection`        | Collection name              |
| `notes`             | User notes                   |
| `read_state`        | Read/unread state            |
| `favorite`          | Favorite flag                |
| `archive_status`    | Archive lifecycle status     |
| `archive_error`     | Last archive error           |
| `archive_rel_dir`   | Relative archive directory   |
| `archive_bytes`     | Archive size                 |
| `created_epoch`     | Created timestamp            |
| `updated_epoch`     | Updated timestamp            |
| `archived_epoch`    | Archive completion timestamp |

Important `echo_stack_content` fields:

| Field           | Meaning                                |
| --------------- | -------------------------------------- |
| `owner_fp`      | Owner fingerprint                      |
| `item_id`       | Echo Stack item id                     |
| `url`           | Original URL                           |
| `final_url`     | Final URL                              |
| `title`         | Indexed title                          |
| `description`   | Indexed description                    |
| `tags_text`     | Indexed tags                           |
| `collection`    | Indexed collection                     |
| `source_file`   | Source file used for extracted content |
| `body_text`     | Searchable extracted body text         |
| `indexed_epoch` | Index timestamp                        |

To be confirmed:

* Exact archive payload root.
* Whether archived files are under user storage, an app-specific storage root, or another internal path.

## Circle Stack

Circle Stack currently has a main SQLite database:

```
/srv/pqnas/circlestack.db
```

Main tables include:

| Table                      | Purpose                                     |
| -------------------------- | ------------------------------------------- |
| `posts`                    | Local posts                                 |
| `post_replies`             | Replies                                     |
| `post_reactions`           | Post reactions                              |
| `reply_reactions`          | Reply reactions                             |
| `post_mentions`            | Post mentions                               |
| `reply_mentions`           | Reply mentions                              |
| `circle_edges`             | Circle/social graph edges                   |
| `introductions`            | User introduction records                   |
| `contact_requests`         | Contact request records                     |
| `federated_post_reactions` | Federated reactions merged into local posts |
| `user_achievement_unlocks` | Server-side achievement unlock history      |

Important `posts` fields:

| Field           | Meaning                           |
| --------------- | --------------------------------- |
| `id`            | Local post id                     |
| `text`          | Post text                         |
| `media_path`    | Post media path                   |
| `created_epoch` | Created timestamp                 |
| `owner_fp`      | Owner fingerprint                 |
| `visibility`    | Visibility state, such as public  |
| `circle_allow`  | Circle visibility allow-list JSON |

Important `post_replies` fields:

| Field           | Meaning                  |
| --------------- | ------------------------ |
| `id`            | Reply id                 |
| `post_id`       | Parent post id           |
| `actor_fp`      | Reply author fingerprint |
| `text`          | Reply text               |
| `media_path`    | Reply media path         |
| `created_epoch` | Created timestamp        |

To be confirmed:

* Exact Circle Stack media payload root.
* Whether post and reply media should be stored as normal File Manager payloads, app-private payloads, or references to existing user files.

Recommended future rule:

Circle Stack should avoid duplicating files where possible. Media should either:

* reference existing logical file paths, or
* store app-owned media in a clearly documented app-private storage root with quota accounting.

## Circle Stack federation

Circle Stack federation uses separate SQLite databases under `/srv/pqnas/config`.

| Database                                                       | Purpose                                            |
| -------------------------------------------------------------- | -------------------------------------------------- |
| `/srv/pqnas/config/circlestack_federation_outbox.sqlite3`      | Local federation events waiting to be published    |
| `/srv/pqnas/config/circlestack_federation_inbox.sqlite3`       | Received federation events waiting to be processed |
| `/srv/pqnas/config/circlestack_federation_remote_feed.sqlite3` | Remote feed/event cache                            |

Main outbox table:

| Table                      | Purpose                                        |
| -------------------------- | ---------------------------------------------- |
| `circle_federation_outbox` | Pending/done/failed outbound federation events |

Important outbox fields:

| Field                | Meaning                              |
| -------------------- | ------------------------------------ |
| `created_epoch`      | Event enqueue time                   |
| `updated_epoch`      | Last status update                   |
| `next_attempt_epoch` | Retry scheduling                     |
| `attempts`           | Publish attempt count                |
| `status`             | Pending/publishing/done/failed state |
| `event_type`         | Federation event type                |
| `circle_id`          | Circle/feed id                       |
| `event_id`           | Stable event id                      |
| `event_key`          | DHT/event key                        |
| `head_key`           | DHT/head key                         |
| `event_json`         | Serialized federation event          |
| `last_error`         | Last publish error                   |

Main inbox table:

| Table                     | Purpose                         |
| ------------------------- | ------------------------------- |
| `circle_federation_inbox` | Received federation event queue |

Main remote feed table:

| Table                           | Purpose                   |
| ------------------------------- | ------------------------- |
| `circle_federation_remote_feed` | Cached remote feed events |

Federation event databases should be covered by backup policy, but pruning policy should also be documented because these can grow over time.

## People contacts

People contacts use a dedicated SQLite database:

```
/srv/pqnas/config/people_contacts.sqlite3
```

Main table:

| Table             | Purpose                   |
| ----------------- | ------------------------- |
| `people_contacts` | Per-owner contact records |

Important fields:

| Field                 | Meaning                       |
| --------------------- | ----------------------------- |
| `id`                  | Contact row id                |
| `owner_fingerprint`   | Owner fingerprint             |
| `subject_user_id`     | Optional linked local user id |
| `subject_fingerprint` | Contact fingerprint           |
| `subject_kind`        | Contact kind                  |
| `display_name`        | Display name                  |
| `nickname`            | Owner-specific nickname       |
| `notes`               | Owner-specific notes          |
| `created_at_epoch`    | Created timestamp             |
| `updated_at_epoch`    | Updated timestamp             |

## Activity log

Activity logs are stored per user, below the user root:

```
<user_root>/.pqnas_activity/activity.sqlite
```

Main table:

| Table             | Purpose                       |
| ----------------- | ----------------------------- |
| `activity_events` | User-visible activity history |

Important fields:

| Field                     | Meaning                         |
| ------------------------- | ------------------------------- |
| `created_at_epoch`        | Event timestamp                 |
| `owner_user_id`           | User whose activity log this is |
| `actor_user_id`           | Actor user id or fingerprint    |
| `actor_display_name`      | Actor display name              |
| `actor_device_name`       | Actor device name               |
| `actor_fingerprint_short` | Short actor fingerprint         |
| `actor_kind`              | User, guest, system, etc.       |
| `event_type`              | Event type                      |
| `scope_type`              | User/workspace/social/etc.      |
| `scope_id`                | Scope id                        |
| `target_kind`             | File/folder/post/etc.           |
| `target_name`             | Human-readable target name      |
| `target_path`             | Target logical path             |
| `message`                 | Renderable message              |
| `details_json`            | Structured event details        |

Activity logs are useful for UI history and achievements. They should be included in backup policy unless deliberately treated as disposable telemetry.

## Achievements

Achievements are partly calculated from existing data and partly persisted as unlock history.

Known persistent location:

```
/srv/pqnas/circlestack.db
```

Known table:

| Table                      | Purpose                                |
| -------------------------- | -------------------------------------- |
| `user_achievement_unlocks` | Server-side achievement unlock history |

Important fields:

| Field              | Meaning                          |
| ------------------ | -------------------------------- |
| `user_fp`          | User fingerprint                 |
| `achievement_id`   | Badge id                         |
| `unlocked_epoch`   | First detected unlock time       |
| `first_seen_epoch` | First server-side sync/check     |
| `last_seen_epoch`  | Last sync/check                  |
| `dismissed_epoch`  | Unlock modal dismissed timestamp |
| `visible`          | Future visibility flag           |
| `pinned`           | Future pinned badge flag         |

Achievements may read from:

* `/srv/pqnas/circlestack.db`
* user activity logs
* Circle Stack federation databases
* People contacts
* user account metadata

Development override file:

```
/srv/pqnas/config/circlestack_achievements_force_all_fp
```

This file should not be treated as production user data.

## Shares and auth configuration

Known JSON and directory state under `/srv/pqnas/config`:

| Path                                            | Purpose                                  |
| ----------------------------------------------- | ---------------------------------------- |
| `/srv/pqnas/config/users.json`                  | User registry                            |
| `/srv/pqnas/config/workspaces.json`             | Workspace registry                       |
| `/srv/pqnas/config/shares.json`                 | Share state                              |
| `/srv/pqnas/config/app_auth.json`               | Device/app auth state and refresh tokens |
| `/srv/pqnas/config/share_invites_v1`            | Share invite state                       |
| `/srv/pqnas/config/share_manifests_v1`          | Share manifests                          |
| `/srv/pqnas/config/share_recipient_sessions_v1` | Share recipient sessions                 |
| `/srv/pqnas/config/share_recipients_v1`         | Share recipient metadata                 |

These files and directories should be considered critical configuration and must be included in full system backup.

## Backup and migration implications

A full backup should include at least:

| Area                        | Include?                                                        | Reason                                               |
| --------------------------- | --------------------------------------------------------------- | ---------------------------------------------------- |
| `/srv/pqnas/config`         | Yes                                                             | Users, workspaces, shares, metadata DBs, auth state  |
| `/srv/pqnas/data`           | Yes                                                             | User files, workspace files, avatars, Trash payloads |
| `/srv/pqnas/circlestack.db` | Yes                                                             | Circle Stack posts, replies, reactions, achievements |
| User activity DBs           | Yes, unless intentionally disposable                            | Activity history and achievement signals             |
| Version blobs               | Yes                                                             | Preserved file history                               |
| Trash payloads              | Yes, if recoverable Trash is expected after restore             | User-recoverable deleted files                       |
| WAL/SHM files               | Usually yes during live backup or use sqlite-safe backup method | SQLite consistency                                   |

For SQLite databases using WAL mode, backup tooling should either:

* stop the service before copying, or
* use SQLite backup APIs, or
* include the database, `-wal`, and `-shm` files consistently.

## Design recommendations

Future apps should follow these rules:

1. Store user-visible payloads in the normal user/workspace storage model whenever practical.
2. Store app metadata in documented SQLite databases or JSON files.
3. Prefer logical file path references over duplicating file payloads.
4. If an app owns private payloads, document the app-private root clearly.
5. Every app-owned payload area must have quota, backup, restore, and garbage-collection behavior.
6. App metadata must be updated when files are moved, renamed, restored, or deleted.
7. Public routes must never expose internal physical paths.
8. Public tokens should be stored as hashes, not raw bearer secrets.
9. WAL-mode SQLite databases need backup-aware handling.
10. Any app that stores large derived data should document retention and cleanup rules.

## Open questions

The following items need a follow-up source-code pass:

| Area                    | Question                                                                    |
| ----------------------- | --------------------------------------------------------------------------- |
| Echo Stack              | Exact archive payload root                                                  |
| Circle Stack            | Exact media payload root for posts and replies                              |
| Reel Stack              | Whether video metadata exists yet or is still planned                       |
| Music Library           | Whether persistent metadata exists yet or is still planned                  |
| Thumbnail/cache storage | Exact root paths and cleanup policy                                         |
| Startup wiring          | Exact db filenames for db_path-injected stores                              |
| Backups                 | Whether every metadata DB is already included in system backup worker       |
| Quota                   | Whether every app-private payload area is included in user quota accounting |
| Garbage collection      | Whether app metadata is cleaned when referenced files disappear             |

## Summary

DNA-Nexus / PQ-NAS already has a strong pattern emerging:

* live files belong to the logical File Manager storage model
* app metadata belongs in SQLite
* app payload duplication should be avoided
* album-style features should reference logical paths
* Trash and file versions store payloads separately but with explicit metadata
* Circle Stack and federation currently have their own dedicated SQLite databases
* per-user activity logs live under the user root
* config JSON and app auth state live under `/srv/pqnas/config`

The preferred long-term rule should be:

App payloads should be stored once, metadata should reference logical paths, and every app-owned storage area must be documented for backup, quota, restore, and cleanup.
