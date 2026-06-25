# main.cpp API Route Inventory

Status: generated draft.

This document is an initial route inventory generated from source code.
Descriptions and auth classifications should be reviewed manually before treating this as authoritative API documentation.

Source values should use stable source file paths, not line numbers, because route line numbers drift during refactors.

Generated: 2026-06-10 12:10:46

## Route Summary

| Method | Route | Auth | Source |
|---|---|---|---|
| `GET` | `/` | Public/static | `server/src/main.cpp:12220` |
| `GET` | `/admin` | Admin session | `server/src/main.cpp:11944` |
| `GET` | `/admin/approvals` | Admin session | `server/src/main.cpp:23043` |
| `GET` | `/admin/apps` | Admin session | `server/src/main.cpp:21206` |
| `GET` | `/admin/audit` | Admin session | `server/src/main.cpp:11935` |
| `GET` | `/admin/settings` | Admin session | `server/src/main.cpp:18784` |
| `GET` | `/admin/stats` | Admin session | `server/src/main.cpp:22039` |
| `GET` | `/admin/users` | Admin session | `server/src/main.cpp:21265` |
| `GET` | `/api/debug/auth/approvals` | Unknown | `server/src/main.cpp:44648` |
| `GET` | `/api/public/auth_mode` | Public token/link | `server/src/main.cpp:11836` |
| `GET` | `/api/public/gallery/album/image` | Public token/link | `server/src/main.cpp:48290` |
| `POST` | `/api/v4/admin/audit/preview-prune` | Admin session | `server/src/main.cpp:18833` |
| `POST` | `/api/v4/admin/audit/prune` | Admin session | `server/src/main.cpp:18855` |
| `GET` | `/api/v4/admin/ping` | Admin session | `server/src/main.cpp:12235` |
| `POST` | `/api/v4/admin/rotate-audit` | Admin session | `server/src/main.cpp:18802` |
| `GET` | `/api/v4/admin/settings` | Admin session | `server/src/main.cpp:18952` |
| `POST` | `/api/v4/admin/settings` | Admin session | `server/src/main.cpp:19271` |
| `POST` | `/api/v4/admin/settings/create-dna-alert-identity` | Admin session | `server/src/main.cpp:20339` |
| `GET` | `/api/v4/admin/settings/dna-alert-identity-info` | Admin session | `server/src/main.cpp:20426` |
| `POST` | `/api/v4/admin/settings/send-dna-alert-contact-request` | Admin session | `server/src/main.cpp:19197` |
| `POST` | `/api/v4/admin/settings/send-dna-alert-contact-request` | Admin session | `server/src/main.cpp:20436` |
| `GET` | `/api/v4/admin/stats/summary` | Admin session | `server/src/main.cpp:22663` |
| `GET` | `/api/v4/admin/stats/trends` | Admin session | `server/src/main.cpp:22064` |
| `POST` | `/api/v4/admin/storage/tiering/migrate_one` | Admin session | `server/src/routes/routes_admin_storage_tiering.cpp` |
| `GET` | `/api/v4/admin/storage/tiering/status` | Admin session | `server/src/routes/routes_admin_storage_tiering.cpp` |
| `GET` | `/api/v4/admin/users` | Admin session | `server/src/main.cpp:22978` |
| `GET` | `/api/v4/admin/users/avatar` | Admin session | `server/src/main.cpp:43323` |
| `POST` | `/api/v4/admin/users/avatar_remove` | Admin session | `server/src/main.cpp:43418` |
| `POST` | `/api/v4/admin/users/avatar_upload` | Admin session | `server/src/main.cpp:43215` |
| `POST` | `/api/v4/admin/users/cleanup_old_storage` | Admin session | `server/src/routes/routes_admin_user_storage_jobs.cpp` |
| `GET` | `/api/v4/admin/users/cleanup_old_storage_status` | Admin session | `server/src/routes/routes_admin_user_storage_jobs.cpp` |
| `POST` | `/api/v4/admin/users/delete` | Admin session | `server/src/main.cpp:44677` |
| `POST` | `/api/v4/admin/users/disable` | Admin session | `server/src/main.cpp:44583` |
| `POST` | `/api/v4/admin/users/enable` | Admin session | `server/src/main.cpp:43155` |
| `POST` | `/api/v4/admin/users/migrate_storage` | Admin session | `server/src/routes/routes_admin_user_storage_jobs.cpp` |
| `GET` | `/api/v4/admin/users/migrate_storage_status` | Admin session | `server/src/routes/routes_admin_user_storage_jobs.cpp` |
| `POST` | `/api/v4/admin/users/status` | Admin session | `server/src/main.cpp:23160` |
| `POST` | `/api/v4/admin/users/storage` | Admin session | `server/src/routes/routes_admin_user_storage.cpp` |
| `GET` | `/api/v4/admin/users/storage_preview` | Admin session | `server/src/routes/routes_admin_user_storage_preview.cpp` |
| `POST` | `/api/v4/admin/users/upsert` | Admin session | `server/src/main.cpp:43060` |
| `GET` | `/api/v4/apps` | User session | `server/src/main.cpp:12046` |
| `GET` | `/api/v4/apps/has` | User session | `server/src/main.cpp:43693` |
| `POST` | `/api/v4/apps/install_bundled` | User session | `server/src/main.cpp:44215` |
| `POST` | `/api/v4/apps/launch_policy` | User session | `server/src/main.cpp:44385` |
| `GET` | `/api/v4/apps/list` | User session | `server/src/main.cpp:43831` |
| `POST` | `/api/v4/apps/uninstall` | User session | `server/src/main.cpp:44495` |
| `POST` | `/api/v4/apps/upload_install` | User session | `server/src/main.cpp:43959` |
| `GET` | `/api/v4/audit/tail` | User session | `server/src/main.cpp:18722` |
| `GET` | `/api/v4/audit/verify` | User session | `server/src/main.cpp:18753` |
| `GET` | `/api/v4/files/archive_manifest` | User session | `server/src/main.cpp:41065` |
| `POST` | `/api/v4/files/cat` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/copy` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/delete` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/du` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/exists` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/favorites` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/favorites/add` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/favorites/remove` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/get` | User session | `server/src/main.cpp:34770` |
| `POST` | `/api/v4/files/hash` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/list` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/mkdir` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/move` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/office_preview` | User session | `server/src/main.cpp:34502` |
| `PUT` | `/api/v4/files/put` | User session | `server/src/routes/routes_files_put.inc` |
| `GET` | `/api/v4/files/read_text` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/restore_version` | User session | `server/src/routes/routes_file_versions_restore.cpp` |
| `POST` | `/api/v4/files/rmdir` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/rmrf` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/save_text` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/search` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/stat` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/stat` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/stat_sel` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/touch` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/tree` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/versions/archive_manifest` | User session | `server/src/main.cpp:41136` |
| `GET` | `/api/v4/files/versions/blob` | User session | `server/src/main.cpp:41202` |
| `POST` | `/api/v4/files/versions/delete` | User session | `server/src/routes/routes_file_versions_manage.cpp` |
| `GET` | `/api/v4/files/versions/download` | User session | `server/src/routes/routes_file_versions_read.cpp` |
| `POST` | `/api/v4/files/versions/flag` | User session | `server/src/routes/routes_file_versions_manage.cpp` |
| `GET` | `/api/v4/files/versions/list` | User session | `server/src/routes/routes_file_versions_read.cpp` |
| `GET` | `/api/v4/files/versions/read_text` | User session | `server/src/routes/routes_file_versions_read.cpp` |
| `GET` | `/api/v4/files/versions/summary` | User session | `server/src/routes/routes_file_versions_manage.cpp` |
| `POST` | `/api/v4/files/versions/unflag` | User session | `server/src/routes/routes_file_versions_manage.cpp` |
| `POST` | `/api/v4/files/write_text` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/zip` | User session | `server/src/main.cpp:34058` |
| `POST` | `/api/v4/files/zip` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/zip_sel` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/gallery/export_sel_zip` | User session | `server/src/main.cpp:47131` |
| `GET` | `/api/v4/gallery/list` | User session | `server/src/main.cpp:34993` |
| `POST` | `/api/v4/gallery/meta/embedded_get` | User session | `server/src/main.cpp:49072` |
| `POST` | `/api/v4/gallery/meta/get` | User session | `server/src/main.cpp:39694` |
| `POST` | `/api/v4/gallery/meta/set` | User session | `server/src/main.cpp:39225` |
| `GET` | `/api/v4/gallery/search` | User session | `server/src/main.cpp:36636` |
| `GET` | `/api/v4/gallery/thumb` | User session | `server/src/main.cpp:38653` |
| `GET` | `/api/v4/gallery/tree_stats` | User session | `server/src/main.cpp:36516` |
| `GET` | `/api/v4/me` | User session | `server/src/main.cpp:20509` |
| `GET` | `/api/v4/me/storage` | User session | `server/src/main.cpp:33050` |
| `GET` | `/api/v4/music/cover` | User session | `server/src/main.cpp:35570` |
| `GET` | `/api/v4/photogallery/stats` | User session | `server/src/main.cpp:39973` |
| `POST` | `/api/v4/poolmgr/add-slot` | User session | `server/src/main.cpp:13189` |
| `POST` | `/api/v4/poolmgr/apply-layout` | User session | `server/src/main.cpp:13681` |
| `POST` | `/api/v4/poolmgr/plan-layout` | User session | `server/src/main.cpp:13518` |
| `POST` | `/api/v4/poolmgr/remove-slot` | User session | `server/src/main.cpp:13277` |
| `POST` | `/api/v4/poolmgr/set-layout` | User session | `server/src/main.cpp:13391` |
| `GET` | `/api/v4/raid/balance-status` | User session | `server/src/main.cpp:14224` |
| `GET` | `/api/v4/raid/discovery` | User session | `server/src/main.cpp:14036` |
| `GET` | `/api/v4/raid/exec-record` | User session | `server/src/main.cpp:13965` |
| `GET` | `/api/v4/raid/exec-record` | User session | `server/src/main.cpp:18489` |
| `POST` | `/api/v4/raid/execute/add-device` | User session | `server/src/main.cpp:16642` |
| `POST` | `/api/v4/raid/execute/convert-mode` | User session | `server/src/main.cpp:15858` |
| `POST` | `/api/v4/raid/execute/create-pool` | User session | `server/src/main.cpp:18020` |
| `POST` | `/api/v4/raid/execute/destroy-pool` | User session | `server/src/main.cpp:17190` |
| `POST` | `/api/v4/raid/execute/remove-device` | User session | `server/src/main.cpp:17538` |
| `POST` | `/api/v4/raid/execute/scrub` | User session | `server/src/main.cpp:14650` |
| `GET` | `/api/v4/raid/health` | User session | `server/src/main.cpp:18587` |
| `GET` | `/api/v4/raid/job` | User session | `server/src/main.cpp:18460` |
| `POST` | `/api/v4/raid/plan/add-device` | User session | `server/src/main.cpp:15307` |
| `POST` | `/api/v4/raid/plan/convert-mode` | User session | `server/src/main.cpp:15645` |
| `POST` | `/api/v4/raid/plan/create-pool` | User session | `server/src/main.cpp:16530` |
| `POST` | `/api/v4/raid/plan/remove-device` | User session | `server/src/main.cpp:16219` |
| `POST` | `/api/v4/raid/plan/scrub` | User session | `server/src/main.cpp:14506` |
| `GET` | `/api/v4/raid/scrub-status` | User session | `server/src/main.cpp:14378` |
| `GET` | `/api/v4/raid/status` | User session | `server/src/main.cpp:15108` |
| `GET` | `/api/v4/reelstack/index` | User session | `server/src/main.cpp:37179` |
| `GET` | `/api/v4/reelstack/meta` | User session | `server/src/main.cpp:37956` |
| `POST` | `/api/v4/reelstack/meta/set` | User session | `server/src/main.cpp:38523` |
| `POST` | `/api/v4/reelstack/scan` | User session | `server/src/main.cpp:37249` |
| `GET` | `/api/v4/reelstack/thumb` | User session | `server/src/main.cpp:37609` |
| `GET` | `/api/v4/reelstack/user_meta` | User session | `server/src/main.cpp:38464` |
| `POST` | `/api/v4/shares/create` | User session | `server/src/main.cpp:46015` |
| `GET` | `/api/v4/shares/list` | User session | `server/src/main.cpp:46625` |
| `POST` | `/api/v4/shares/pq/enroll` | User session | `server/src/main.cpp:44754` |
| `POST` | `/api/v4/shares/pq/open` | User session | `server/src/main.cpp:45567` |
| `GET` | `/api/v4/shares/pq/open/chunk` | User session | `server/src/main.cpp:45439` |
| `POST` | `/api/v4/shares/pq/open/init` | User session | `server/src/main.cpp:45262` |
| `POST` | `/api/v4/shares/pq/recipient/update` | User session | `server/src/main.cpp:45768` |
| `POST` | `/api/v4/shares/revoke` | User session | `server/src/main.cpp:46515` |
| `POST` | `/api/v4/snapshots/create` | User session | `server/src/routes/routes_snapshots_create.cpp` |
| `GET` | `/api/v4/snapshots/info` | User session | `server/src/routes/routes_snapshots_browse.cpp` |
| `GET` | `/api/v4/snapshots/list` | User session | `server/src/routes/routes_snapshots_browse.cpp` |
| `POST` | `/api/v4/snapshots/restore/confirm` | User session | `server/src/routes/routes_snapshots_restore.cpp` |
| `POST` | `/api/v4/snapshots/restore/prepare` | User session | `server/src/routes/routes_snapshots_restore.cpp` |
| `GET` | `/api/v4/snapshots/restore/status` | User session | `server/src/routes/routes_snapshots_restore.cpp` |
| `GET` | `/api/v4/snapshots/volumes` | User session | `server/src/routes/routes_snapshots_browse.cpp` |
| `GET` | `/api/v4/storage/disks` | User session | `server/src/main.cpp:12243` |
| `GET` | `/api/v4/storage/overview` | User session | `server/src/main.cpp:13835` |
| `GET` | `/api/v4/storage/pools` | User session | `server/src/main.cpp:12376` |
| `POST` | `/api/v4/storage/pools/rename` | User session | `server/src/main.cpp:12936` |
| `POST` | `/api/v4/storage/pools/set-name` | User session | `server/src/main.cpp:12754` |
| `GET` | `/api/v4/storage/status` | User session | `server/src/main.cpp:12269` |
| `GET` | `/api/v4/system` | User session | `server/src/main.cpp:11853` |
| `GET` | `/api/v4/system/drives` | User session | `server/src/main.cpp:23979` |
| `POST` | `/api/v4/system/drives/refresh-now` | User session | `server/src/main.cpp:24094` |
| `POST` | `/api/v4/system/drives/selftest/start` | User session | `server/src/main.cpp:24114` |
| `GET` | `/api/v4/system/storage` | User session | `server/src/main.cpp:23949` |
| `POST` | `/api/v4/uploads/cancel` | User session | `server/src/routes/routes_uploads_chunked.cpp` |
| `PUT` | `/api/v4/uploads/chunk` | User session | `server/src/routes/routes_uploads_chunked.cpp` |
| `POST` | `/api/v4/uploads/finish` | User session | `server/src/routes/routes_uploads_chunked.cpp` |
| `POST` | `/api/v4/uploads/start` | User session | `server/src/routes/routes_uploads_chunked.cpp` |
| `GET` | `/api/v4/user/profile` | User session | `server/src/main.cpp:20635` |
| `POST` | `/api/v4/user/profile/avatar_remove` | User session | `server/src/main.cpp:43626` |
| `POST` | `/api/v4/user/profile/avatar_upload` | User session | `server/src/main.cpp:43465` |
| `POST` | `/api/v4/user/profile/update` | User session | `server/src/main.cpp:20679` |
| `GET` | `/api/v4/users/avatar` | User session | `server/src/main.cpp:43341` |
| `POST` | `/api/v5/verify` | Unknown | `server/src/main.cpp:21152` |
| `GET` | `/app` | Unknown | `server/src/main.cpp:12200` |
| `GET` | `/app` | Unknown | `server/src/main.cpp:12210` |
| `GET` | `/apps/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/(.*)` | Unknown | `server/src/main.cpp:11871` |
| `GET` | `/pq/invite/([A-Za-z0-9_-]+)` | Unknown | `server/src/main.cpp:49368` |
| `GET` | `/s/([A-Za-z0-9_-]+)` | Unknown | `server/src/main.cpp:48350` |
| `GET` | `/static/(.+)` | Public/static | `server/src/main.cpp:23063` |
| `GET` | `/static/admin.js` | Admin session | `server/src/main.cpp:11965` |
| `GET` | `/static/admin_approvals.js` | Admin session | `server/src/main.cpp:23053` |
| `GET` | `/static/admin_apps.js` | Admin session | `server/src/main.cpp:21252` |
| `GET` | `/static/admin_audit.js` | Admin session | `server/src/main.cpp:12164` |
| `GET` | `/static/admin_badges.js` | Admin session | `server/src/main.cpp:21304` |
| `GET` | `/static/admin_settings.js` | Admin session | `server/src/main.cpp:18936` |
| `GET` | `/static/admin_stats.js` | Admin session | `server/src/main.cpp:22052` |
| `GET` | `/static/admin_users.js` | Admin session | `server/src/main.cpp:21275` |
| `GET` | `/static/app.js` | Public/static | `server/src/main.cpp:11959` |
| `GET` | `/static/pqnas_v5.js` | Public/static | `server/src/main.cpp:12178` |
| `GET` | `/static/system.js` | Public/static | `server/src/main.cpp:11922` |
| `GET` | `/static/theme.css` | Public/static | `server/src/main.cpp:21282` |
| `GET` | `/static/theme.js` | Public/static | `server/src/main.cpp:21293` |
| `GET` | `/static/wait_approval.js` | Public/static | `server/src/main.cpp:21199` |
| `GET` | `/success` | Unknown | `server/src/main.cpp:12192` |
| `GET` | `/system` | Unknown | `server/src/main.cpp:23930` |
| `GET` | `/wait-approval` | Unknown | `server/src/main.cpp:21191` |

## Route Details

### GET `/`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12220`

---

### GET `/admin`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11944`

---

### GET `/admin/approvals`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23043`

---

### GET `/admin/apps`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21206`

---

### GET `/admin/audit`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11935`

---

### GET `/admin/settings`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18784`

---

### GET `/admin/stats`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:22039`

---

### GET `/admin/users`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21265`

---

### GET `/api/debug/auth/approvals`

Purpose:
Authentication/session related endpoint.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:44648`

---

### GET `/api/public/auth_mode`

Purpose:
Authentication/session related endpoint.

Auth:
Public token/link

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11836`

---

### GET `/api/public/gallery/album/image`

Purpose:
Gallery/photo related endpoint.

Auth:
Public token/link

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:48290`

---

### POST `/api/v4/admin/audit/preview-prune`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18833`

---

### POST `/api/v4/admin/audit/prune`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18855`

---

### GET `/api/v4/admin/ping`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12235`

---

### POST `/api/v4/admin/rotate-audit`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18802`

---

### GET `/api/v4/admin/settings`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18952`

---

### POST `/api/v4/admin/settings`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:19271`

---

### POST `/api/v4/admin/settings/create-dna-alert-identity`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:20339`

---

### GET `/api/v4/admin/settings/dna-alert-identity-info`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:20426`

---

### POST `/api/v4/admin/settings/send-dna-alert-contact-request`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:19197`

---

### POST `/api/v4/admin/settings/send-dna-alert-contact-request`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:20436`

---

### GET `/api/v4/admin/stats/summary`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:22663`

---

### GET `/api/v4/admin/stats/trends`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:22064`

---

### POST `/api/v4/admin/storage/tiering/migrate_one`

Purpose:
Migrate one landed tiering file from the landing tier to its final storage tier.

Auth:
Admin session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `fingerprint`: required user fingerprint
- `path`: required user-relative file path

Validation and behavior:
- requires admin auth
- requires same-origin before mutation
- validates request JSON
- trims and validates `fingerprint` and `path`
- normalizes the user-relative path with strict path rules
- calls the configured one-file tiering migration implementation
- writes audit event

Response:
`200 OK` JSON:

- `ok`
- `fingerprint`
- `path`

Errors:
- `400 bad_request`
- `500 migration_failed`
- `500 server_error`

Source:
`server/src/routes/routes_admin_storage_tiering.cpp`

---

### GET `/api/v4/admin/storage/tiering/status`

Purpose:
Read storage tiering status.

Auth:
Admin session. This is a read/status route.

Request:
No required query parameters.

Validation and behavior:
- requires admin auth
- calls the configured tiering status provider
- returns provider JSON as-is

Response:
`200 OK` JSON from the tiering status provider.

Errors:
- `500 server_error`

Source:
`server/src/routes/routes_admin_storage_tiering.cpp`

---

### GET `/api/v4/admin/users`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:22978`

---

### GET `/api/v4/admin/users/avatar`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43323`

---

### POST `/api/v4/admin/users/avatar_remove`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43418`

---

### POST `/api/v4/admin/users/avatar_upload`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43215`

---

### POST `/api/v4/admin/users/cleanup_old_storage`

Purpose:
Create a background cleanup job for a user's old storage copy after migration.

Auth:
Admin session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `fingerprint`: required user fingerprint
- `expected_active_pool_id`: required pool id expected to be the user's current active pool
- `old_pool_id`: required old pool id to clean up

Validation and behavior:
- requires admin auth
- requires same-origin before mutation
- validates request JSON
- refuses cleanup when active and old pool are the same
- enqueues a cleanup job
- writes audit event with job id and pool ids

Response:
`200 OK` JSON from the cleanup job enqueue implementation.

Errors:
- `400 bad_request`
- `409 same_pool`
- `500 enqueue_failed`
- `500 server_error`

Source:
`server/src/routes/routes_admin_user_storage_jobs.cpp`

---

### GET `/api/v4/admin/users/cleanup_old_storage_status`

Purpose:
Read one old-storage cleanup job record.

Auth:
Admin session. This is a read/status route.

Request:
Query parameters:

- `job_id`: required lowercase 64-character SHA-256 style job id

Validation and behavior:
- requires admin auth
- validates job id format
- reads the cleanup job record

Response:
`200 OK` JSON:

- `ok`
- `job`

Errors:
- `400 bad_job_id`
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_admin_user_storage_jobs.cpp`

---

### POST `/api/v4/admin/users/delete`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:44677`

---

### POST `/api/v4/admin/users/disable`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:44583`

---

### POST `/api/v4/admin/users/enable`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43155`

---

### POST `/api/v4/admin/users/migrate_storage`

Purpose:
Create a background job to migrate a user's storage to another pool.

Auth:
Admin session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `fingerprint`: required user fingerprint
- `pool_id`: required destination pool id

Validation and behavior:
- requires admin auth
- requires same-origin before mutation
- validates request JSON
- resolves a user storage migration plan
- refuses missing users, unallocated storage, invalid destination, or same-pool migration
- enqueues a migration job
- writes audit event with job id and pool transition

Response:
`200 OK` JSON from the migration job enqueue implementation.

Errors:
- `400 bad_request`
- `404 resolve_failed`
- `409 same_pool`
- `500 enqueue_failed`
- `500 server_error`

Source:
`server/src/routes/routes_admin_user_storage_jobs.cpp`

---

### GET `/api/v4/admin/users/migrate_storage_status`

Purpose:
Read one user storage migration job record.

Auth:
Admin session. This is a read/status route.

Request:
Query parameters:

- `job_id`: required lowercase 64-character SHA-256 style job id

Validation and behavior:
- requires admin auth
- validates job id format
- reads the migration job record

Response:
`200 OK` JSON:

- `ok`
- `job`

Errors:
- `400 bad_job_id`
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_admin_user_storage_jobs.cpp`

---

### POST `/api/v4/admin/users/status`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23160`

---

### POST `/api/v4/admin/users/storage`

Purpose:
Allocate or update storage/quota for one user.

Auth:
Admin session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `fingerprint`: required user fingerprint
- `quota_gb`: required numeric quota in GiB
- `pool_id`: optional target storage pool. Defaults to `default`.
- `force`: optional boolean. Required to update an already allocated user.

Validation and behavior:
- requires admin auth
- requires same-origin before mutation
- validates fingerprint and user existence
- refuses users that are not enabled/approved
- validates quota and prevents negative/overflow values
- validates pool existence and pool capacity
- prevents quota overcommit across users and workspaces on the target pool
- computes canonical user root path and ensures it stays under the selected data root
- creates the user directory if needed
- refuses quota below current usage
- updates `storage_state`, `quota_bytes`, `root_rel`, `storage_pool_id`, `storage_set_at`, and `storage_set_by`
- writes audit events

Response:
`200 OK` JSON:

- `ok`
- `fingerprint`
- `pool_id`
- `storage_state`
- `quota_bytes`
- `root_rel`
- `storage_set_at`
- `storage_set_by`

Errors:
- `400 bad_request`
- `403 user_not_approved`
- `404 not_found`
- `404 pool_not_found`
- `409 already_allocated`
- `409 pool_quota_overcommit`
- `409 quota_below_used_bytes`
- `500 server_error`

Source:
`server/src/routes/routes_admin_user_storage.cpp`

---

### GET `/api/v4/admin/users/storage_preview`

Purpose:
Preview storage/quota allocation impact for one user and target storage pool.

Auth:
Admin session. This is a read/preview route.

Request:
Query parameters:

- `fingerprint`: required user fingerprint
- `pool_id`: optional target pool id. Defaults to `default`.

Validation and behavior:
- requires admin authentication
- validates fingerprint format
- validates/resolves the requested pool
- computes the canonical user root path
- reads pool total/free bytes
- estimates current user used bytes
- reports already allocated user/workspace quota on the target pool
- does not mutate user storage or quota settings

Response:
`200 OK` JSON:

- `ok`
- `fingerprint`
- `pool_id`
- `used_bytes`
- `current_quota_bytes`
- `pool_total_bytes`
- `pool_free_bytes`
- `allocated_other_bytes`
- `allocated_total_bytes`
- `allocated_user_bytes`
- `allocated_workspace_bytes`
- `remaining_allocatable_bytes`

Errors:
- `400 bad_request`
- `404 not_found`
- `404 pool_not_found`
- `500 pool_statvfs_failed`
- `500 server_error`

Source:
`server/src/routes/routes_admin_user_storage_preview.cpp`

---

### POST `/api/v4/admin/users/upsert`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43060`

---

### GET `/api/v4/apps`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12046`

---

### GET `/api/v4/apps/has`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43693`

---

### POST `/api/v4/apps/install_bundled`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:44215`

---

### POST `/api/v4/apps/launch_policy`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:44385`

---

### GET `/api/v4/apps/list`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43831`

---

### POST `/api/v4/apps/uninstall`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:44495`

---

### POST `/api/v4/apps/upload_install`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43959`

---

### GET `/api/v4/audit/tail`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18722`

---

### GET `/api/v4/audit/verify`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18753`

---

### GET `/api/v4/files/archive_manifest`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41065`

---

### POST `/api/v4/files/cat`

Purpose:
Return a bounded text preview of an existing file.

Auth:
User session. Requires allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file path
- `max_bytes`: optional maximum bytes to return. Defaults to 65536 and is clamped to 1..1048576.

Validation:
- `path` must be present
- path must resolve under the user's storage
- symlinks are rejected
- target must exist and be a regular file
- returned preview bytes must not contain NUL bytes

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `bytes_total`: full file size
- `bytes_returned`: returned byte count
- `truncated`: whether the preview was truncated
- `text`: returned text preview

Errors:
- `400 bad_request` for missing path, invalid path, non-file target, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the file does not exist
- `415 unsupported_media_type` when binary/NUL content is detected
- `500 server_error` when opening or reading fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/copy`

Purpose:
Copy a user-visible file to a destination path.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `from`: required source user-relative file path
- `to`: required destination user-relative file path

Validation:
- `from` and `to` must be present
- both paths are normalized with strict user-relative path rules
- source and destination must not be the same path
- source must resolve through the metadata-aware logical item resolver
- source must be a regular file
- destination must not already exist logically
- physical symlink sources and symlink destinations are rejected
- destination parent chain is checked before and after directory creation
- metadata index must be available

Behavior:
- Only file copy is supported; directory copy is not supported yet.
- The copy uses a temporary `*.tmp.copy.*` file in the destination directory, then renames it into place.
- If a physical destination file exists, it may be overwritten after validation.
- Quota is checked using the byte delta between source size and any existing destination size.
- Metadata is inserted/updated for the copied file.
- User file activity is recorded best-effort.

Response:
`200 OK` JSON:

- `ok`: `true`
- `from`: requested source path
- `to`: requested destination path
- `type`: `file`
- `src_bytes`: source file size
- `dst_old_bytes`: previous destination file size, if overwritten
- `delta_bytes`: additional quota impact
- `overwrote`: whether an existing physical destination file was overwritten

Errors:
- `400 bad_request` for missing paths, invalid paths, same path, non-file source, non-file destination, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `403 quota_exceeded` when the copy would exceed quota
- `404 not_found` when the source path does not exist
- `409 dest_exists` when the destination exists logically
- `409 path_conflict` when destination parent, temporary path, or destination state is unsafe
- `500 server_error` when metadata access, destination stat, directory creation, copy, rename, overwrite, or metadata upsert fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/delete`

Purpose:
Move a file or directory to trash.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file or directory path

Validation:
- `path` must be present
- path is normalized with strict user-relative path rules
- live file locks are checked before delete/trash movement
- path is resolved through the metadata-aware logical item resolver
- metadata index must be available
- root deletion is refused
- symlinks are rejected
- directory trees are scanned before trash movement and rejected if symlinks are found
- target is rechecked immediately before trash movement to avoid symlink/type-swap races
- mixed storage-root directory deletes are currently rejected

Response:
For file trash movement, `200 OK` JSON:

- `ok`: `true`
- `fingerprint_hex`: authenticated user's fingerprint
- `path`: requested user-relative path
- `type`: `file`
- `freed_bytes`: file size moved to trash
- `trash_id`: trash entry identifier

For directory trash movement, `200 OK` JSON:

- `ok`: `true`
- `fingerprint_hex`: authenticated user's fingerprint
- `path`: requested user-relative path
- `type`: `dir`
- `freed_bytes`: total file bytes moved to trash
- `trash_id`: trash entry identifier

Notes:
- Despite the source comment saying `DELETE`, this route is currently registered as `POST /api/v4/files/delete`.
- This route uses `TrashService` and records best-effort `file.trashed` activity.
- Metadata, favorites, gallery metadata, and Reel Stack metadata are cleaned up best-effort or transactionally depending on the item path/source.
- This route enforces same-origin cookie mutation protection.

Errors:
- `400 bad_request` for missing path, invalid path, root deletion attempt, unsupported type, non-file/non-directory target, or symlink use
- `403 forbidden` when the user record is missing
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the target path does not exist
- `409 path_conflict` for locks, symlinks in directory trees, type changes before delete, or unsafe path conflicts
- `409 unsupported` when deleting a directory across mixed storage roots is not supported
- `500 server_error` when metadata access, tree scanning, trash movement, or cleanup fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/du`

Purpose:
Calculate disk-usage style metadata for a user-visible file or directory.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
Query parameters:

- `path`: required user-relative file or directory path

Validation:
- `path` must be present
- `path` is resolved with strict user path containment rules
- symlinked parent chains and symlink targets are rejected
- the target must exist
- the target must be a regular file or directory

Response:
For a regular file, `200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `type`: `file`
- `bytes_total`: file size in bytes
- `files`: `1`
- `dirs`: `0`

For a directory, `200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `type`: `dir`
- `bytes_total`: total bytes of regular files under the directory
- `files`: number of regular files counted
- `dirs`: number of directories counted, including the root directory

Notes:
- Directory traversal skips permission-denied entries where possible.
- Symlinks are not followed or counted.
- If traversal fails, the route fails closed with a server error.

Errors:
- `400 bad_request` for missing path, invalid path, symlink use, or unsupported target type
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the path does not exist
- `500 server_error` when directory traversal fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/exists`

Purpose:
Check whether a user-relative path exists and return its basic type metadata.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
Query parameters:

- `path`: required user-relative path

Validation:
- `path` must be present
- invalid paths are rejected with `400 bad_request`
- missing but syntactically valid paths return `exists: false`
- symlinked paths are treated as missing rather than exposed

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `exists`: boolean
- `type`: `file`, `dir`, `other`, or `missing`
- `bytes`: file size in bytes for regular files, otherwise `0`

Errors:
- `400 bad_request` for missing or invalid path
- `403 storage_unallocated` when the authenticated user has no allocated storage

Source:
`server/src/routes/routes_files_core.inc`

---

### GET `/api/v4/files/favorites`

Purpose:
List the authenticated user's favorite file and folder entries.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
No JSON body. Uses the current authenticated user session.

Response:
`200 OK` JSON:

- `ok`: `true`
- `items`: array of favorite entries:
  - `path`: user-relative path
  - `type`: `file` or `dir`
  - `added_at`: timestamp stored by the favorites backend

Errors:
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `500 server_error` when favorites storage cannot be read

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/favorites/add`

Purpose:
Add a file or folder to the authenticated user's favorites.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
JSON body:

- `path`: user-relative file or folder path
- `type`: `file` or `dir`

Validation:
- request body must be valid JSON
- `path` is normalized with strict user-relative path rules
- `type` must be `file` or `dir`
- the target path must resolve to an existing logical item
- the resolved item type must match the requested `type`

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: normalized user-relative path
- `type`: `file` or `dir`

Errors:
- `400 bad_request` for invalid JSON, invalid path, or invalid type
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the path does not exist
- `409 type_mismatch` when the requested type does not match the resolved item
- `500 server_error` when adding the favorite fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/favorites/remove`

Purpose:
Remove a file or folder from the authenticated user's favorites.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
JSON body:

- `path`: user-relative file or folder path
- `type`: `file` or `dir`

Validation:
- request body must be valid JSON
- `path` is normalized with strict user-relative path rules
- `type` must be `file` or `dir`

Notes:
- This route removes the favorite entry by normalized path and type.
- It does not require the target item to still exist, which allows stale favorites to be removed cleanly.

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: normalized user-relative path
- `type`: `file` or `dir`

Errors:
- `400 bad_request` for invalid JSON, invalid path, or invalid type
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `500 server_error` when removing the favorite fails

Source:
`server/src/routes/routes_files_core.inc`

---

### GET `/api/v4/files/get`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:34770`

---

### POST `/api/v4/files/hash`

Purpose:
Calculate a digest for an existing user-visible file.

Auth:
User session. Requires allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file path
- `algo`: optional digest algorithm. Currently only `sha256` is supported.

Validation:
- `path` must be present
- unsupported algorithms are rejected
- path must resolve to an existing regular file
- symlinks are rejected

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `algo`: digest algorithm
- `bytes`: file size in bytes
- `digest_hex`: digest as lowercase hex

Errors:
- `400 bad_request` for missing path, invalid path, unsupported algorithm, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the file does not exist or is not a regular file
- `500 server_error` when hashing fails

Source:
`server/src/routes/routes_files_core.inc`

---

### GET `/api/v4/files/list`

Purpose:
List the immediate children of a user-visible directory.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
Query parameters:

- `path`: optional user-relative directory path. Empty or omitted means the authenticated user's storage root.

Validation:
- `path` is normalized with strict user-relative path rules when provided
- symlinked directories and symlink children are not exposed
- reserved internal names such as `.pqnas` are skipped
- non-root paths must resolve either as a physical directory or as metadata-backed children

Response:
`200 OK` JSON:

- `ok`: `true`
- `fingerprint_hex`: authenticated user's fingerprint
- `path`: normalized listed path
- `items`: array of immediate children:
  - `name`: item name
  - `type`: `file` or `dir`
  - `size_bytes`: file size for files, `0` for directories
  - `mtime_unix`: best-effort modification time as Unix timestamp

Notes:
- This route does not recurse.
- It merges legacy filesystem children and metadata-backed immediate children.
- Metadata entries win over legacy entries when names overlap.
- The response is capped at 5000 items.

Errors:
- `400 bad_request` for invalid paths or unsupported symlink use
- `403 forbidden` when the user record is missing
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when a non-root directory cannot be found
- `500 server_error` when metadata listing fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/mkdir`

Purpose:
Create a directory under the authenticated user's storage.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative directory path

Validation:
- `path` must be present
- `path` is resolved with strict user path containment rules
- existing directory chains are checked so symlinked path components are not used
- after creation, the resulting directory chain is checked again

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path

Notes:
- Parent directories are created as needed.
- A best-effort `folder.created` file activity entry is recorded when a new directory was actually created.

Errors:
- `400 bad_request` for missing or invalid path
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `409 path_conflict` when the directory chain is invalid or contains unsafe components
- `500 server_error` when directory creation fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/move`

Purpose:
Move or rename a user-visible file or directory.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `from`: required source user-relative path
- `to`: required destination user-relative path

Validation:
- `from` and `to` must be present
- both paths are normalized with strict user-relative path rules
- source and destination must not be the same path
- destination ancestor paths must not resolve to an existing file
- source must resolve through the metadata-aware logical item resolver
- destination must not already exist logically
- moving a directory into itself or one of its descendants is refused
- live file locks are checked for both source and destination
- metadata index must be available
- symlinks in source or destination parent chains are rejected
- mixed storage-root directory moves are currently rejected

Behavior:
- Files are moved by physical rename when possible.
- Directories are moved by physical rename when possible.
- Cross-device moves may fall back to copy-and-remove after symlink scanning.
- File version history is moved best-effort after successful filesystem rename.
- Metadata is renamed after the physical move.
- Favorites, gallery metadata, gallery albums, and Reel Stack metadata are renamed best-effort where applicable.
- User file activity is recorded best-effort.

Response:
For file move, `200 OK` JSON:

- `ok`: `true`
- `from`: requested source path
- `to`: requested destination path
- `type`: `file`
- `bytes`: moved file size

For directory move, `200 OK` JSON:

- `ok`: `true`
- `from`: requested source path
- `to`: requested destination path
- `type`: `dir`
- `bytes`: `0`

Errors:
- `400 bad_request` for missing paths, invalid paths, same path, moving a directory into itself, unsupported source type, non-file source in file branch, non-dir source in dir branch, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the source path does not exist
- `409 dest_exists` when the destination already exists
- `409 locked` when source or destination is locked
- `409 path_conflict` when destination parent conflicts or path changes unsafely
- `409 unsupported` when moving a directory layout is not supported
- `500 server_error` when lock checks, metadata access, filesystem move, or metadata rename fails

Source:
`server/src/routes/routes_files_core.inc`

---

### GET `/api/v4/files/office_preview`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:34502`

---

### PUT `/api/v4/files/put`

Purpose:
Upload raw bytes to a user-relative file path.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file path
- `overwrite`: optional boolean-like value. Accepted true values include `1`, `true`, and `yes`.

Body:
Raw bytes streamed from the request body.

Required headers:

- `Content-Length`: required so upload size and quota can be checked before writing

Validation:
- `path` must be present and normalized with strict user-relative path rules
- live write locks are checked for the target path
- uploading under a path whose ancestor is an existing file is rejected
- uploading to a logical directory path is rejected
- metadata index must be available
- transport max upload size is enforced
- quota is checked before writing
- symlinked parent paths, symlink targets, and unsafe temp paths are rejected
- existing file conflicts return `409 file_exists` unless overwrite is enabled
- target is rechecked before final rename

Behavior:
- Streams request body to a temporary file.
- Verifies written byte count matches `Content-Length`.
- Preserves previous live file version before overwrite.
- Renames temporary file into place.
- Updates file location metadata.
- Supports storage tiering landing pool when enabled.
- Cleans up old physical file after successful overwrite if the logical file moved to a different physical path.
- Records best-effort `file.uploaded` activity.

Response:
`200 OK` JSON:

- `ok`: `true`
- `fingerprint_hex`: authenticated user's fingerprint
- `path`: requested user-relative path
- `bytes`: bytes written
- `overwrite`: whether overwrite mode was enabled

Errors:
- `400 bad_request` for invalid path, content-length mismatch, stream errors, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `403 quota_exceeded` or related quota response when upload would exceed quota
- `409 file_exists` when target exists and overwrite is not enabled
- `409 path_conflict` when ancestor, target, or temp path is unsafe
- `411 length_required` when `Content-Length` is missing
- `413 transport_limit_exceeded` when request body exceeds transport max
- `500 server_error` when metadata access, destination inspection, directory creation, temp preparation, version preservation, rename, metadata upsert, or write fails

Source:
`server/src/routes/routes_files_put.inc`

---

### GET `/api/v4/files/read_text`

Purpose:
Read a full UTF-8 text file for browser editing.

Auth:
User session. Requires allocated user storage.

Request:
Query parameters:

- `path`: required user-relative existing file path

Validation:
- `path` must be present
- target must resolve to an existing regular file
- symlinks are rejected
- file size must not exceed the browser text-edit limit
- file must look like text
- file content must be valid UTF-8 after optional UTF-8 BOM stripping

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: normalized user-relative path
- `name`: file name
- `mime`: guessed text MIME type
- `encoding`: `utf-8`
- `had_utf8_bom`: whether a UTF-8 BOM was stripped
- `bytes`: file size in bytes
- `mtime_epoch`: modification timestamp
- `sha256`: file SHA-256 digest
- `text`: decoded UTF-8 text content

Errors:
- `400 bad_request` for missing path, invalid path, symlink use, or non-file target
- `400 not_text` when the file does not look like text
- `400 decode_failed` when the file is not valid UTF-8 text
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the file does not exist
- `413 too_large` when the file exceeds the browser text-edit limit
- `500 server_error` when reading or hashing fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/restore_version`

Purpose:
Restore a stored file version back to the live file path.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
JSON body:

- `path`: required user-relative path
- `version_id`: required version id

Validation and behavior:
- requires same-origin before auth/body processing
- validates JSON body
- validates and normalizes `path`
- checks that the user's storage is allocated
- resolves the requested version blob
- preserves or handles the current live file according to restore implementation
- restores the version to the live path and updates live metadata

Response:
`200 OK` JSON on successful restore.

Errors:
- `400 bad_request`
- `403 storage_unallocated`
- `403 forbidden` / origin mismatch
- `404 not_found`
- `409 path_conflict`
- `500 server_error`

Source:
`server/src/routes/routes_file_versions_restore.cpp`

---

### POST `/api/v4/files/rmdir`

Purpose:
Remove an empty directory from the authenticated user's storage.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative directory path

Validation:
- `path` must be present
- root or root-like paths are refused
- path is normalized with strict user-relative path rules
- live file locks are checked before removal
- target must resolve to an existing logical directory
- metadata-backed directory descendants are checked first
- physical directory must be empty
- target is rechecked immediately before removal to avoid symlink/type-swap races

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path

Notes:
- This is empty-directory removal only.
- Favorites under the removed directory are cleaned up best-effort.

Errors:
- `400 bad_request` for missing path, invalid path, root deletion attempt, non-directory target, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the directory does not exist
- `409 not_empty` when the directory has contents
- `409 path_conflict` when the path changes before deletion
- `500 server_error` when metadata inspection or directory removal fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/rmrf`

Purpose:
Permanently remove a file or directory tree from the authenticated user's storage.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file or directory path

Validation:
- `path` must be present
- root or root-like paths are refused
- path is resolved through the metadata-aware logical item resolver
- the route serializes writes for the target logical path
- metadata index must be available
- symlinks are rejected
- directory trees are scanned before deletion and rejected if symlinks are found
- target is rechecked immediately before deletion to avoid symlink/type-swap races
- mixed storage-root directory deletes are currently rejected

Response:
For file deletion, `200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `type`: `file`
- `removed_files`: `1`
- `removed_dirs`: `0`
- `removed_bytes`: removed file size

For directory deletion, `200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `type`: `dir`
- `removed_files`: number of removed files
- `removed_dirs`: number of removed directories
- `removed_bytes`: removed file bytes

Notes:
- Unlike `/api/v4/files/delete`, this route removes files/directories directly instead of moving them to trash.
- Metadata and favorites are cleaned up after deletion.
- This route enforces same-origin cookie mutation protection.

Errors:
- `400 bad_request` for missing path, root deletion attempt, unsupported type, non-file/non-directory target, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the target path does not exist
- `409 path_conflict` for symlinks in directory trees, type changes before delete, or unsafe path conflicts
- `409 unsupported` when deleting a directory across mixed storage roots is not supported
- `500 server_error` when metadata access, tree scanning, remove/remove_all, or cleanup fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/save_text`

Purpose:
Save raw UTF-8 text request body to a file path.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file path

Body:
Raw UTF-8 text content. Empty content is allowed.

Validation:
- `path` must be present
- request body must not contain NUL bytes
- path is resolved with strict user path containment rules
- symlinked parent paths and symlink destinations are rejected
- existing destination must be a regular file
- quota is checked using only the byte delta when overwriting
- destination is rechecked before overwrite and before final rename

Behavior:
- Writes to a temporary file in the same directory.
- Renames the temporary file into place.
- Existing files are overwritten after validation.
- This route does not use the JSON body shape used by `/api/v4/files/write_text`.

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `new_bytes`: new file size
- `old_bytes`: previous file size
- `delta_bytes`: quota delta
- `overwrote`: whether an existing file was overwritten

Errors:
- `400 bad_request` for missing path, invalid path, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `403 forbidden` for quota policy denial
- `409 path_conflict` when the destination changes before save or parent path is unsafe
- `413 quota_exceeded` when saving would exceed quota
- `415 unsupported_media_type` when body contains binary/NUL content
- `500 server_error` when destination inspection, parent creation, temp write, overwrite removal, or final rename fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/search`

Purpose:
Search file and directory names under a user-visible directory.

Auth:
User session. Requires allocated user storage.

Request:
Query parameters:

- `path`: optional user-relative directory path. Empty, `.`, or `./` means the authenticated user's storage root.
- `q`: required search query
- `max`: optional maximum results. Defaults to 200 and is clamped to 1..2000.

Validation:
- `q` must be present
- `q` length must not exceed 128 bytes
- base path must resolve to an existing directory
- symlinked base paths are rejected
- symlink children are skipped
- recursive scan is hard-capped

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: searched path, or `.`
- `q`: query
- `max`: effective result cap
- `scanned`: number of entries scanned
- `matched`: number of matching entries found
- `truncated`: whether results were truncated
- `scan_capped`: whether the hard scan cap was hit
- `results`: array of matching items:
  - `path`
  - `name`
  - `type`
  - `bytes` for files

Errors:
- `400 bad_request` for missing/too-long query, invalid path, non-directory path, or symlinked base path
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the base path does not exist
- `500 server_error` when directory walking fails

Source:
`server/src/routes/routes_files_core.inc`

---

### GET `/api/v4/files/stat`

Purpose:
Return metadata for a user-visible file, directory, or storage root.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
Query parameters:

- `path`: optional user-relative path. `.`, `./`, `/`, empty, or omitted means the authenticated user's storage root.

Validation:
- non-root paths are resolved through the metadata-aware logical item resolver
- symlinks are rejected
- missing paths return `404 not_found`

Response:
`200 OK` JSON common fields:

- `ok`: `true`
- `path`: requested path, or `.` for root
- `path_norm`: normalized UI path beginning with `/`
- `name`: item display name
- `type`: `file`, `dir`, or `other`
- `exists`: `true`
- `mtime_epoch`: best-effort modification time when available
- `mode_octal`: best-effort permission mode

For files, additional fields:

- `bytes`: file size in bytes
- `mime`: guessed MIME type from file extension
- `is_text`: boolean lightweight text/binary check

For directories, additional fields:

- `children`: immediate child counts:
  - `files`
  - `dirs`
  - `other`
- `bytes_recursive`: recursive byte count of regular files
- `recursive_scanned_entries`: number of recursive entries scanned
- `recursive_complete`: whether recursive scan completed within limits
- `scan_cap`: hard recursive entry cap
- `time_cap_ms`: soft recursive scan time cap

Notes:
- Recursive directory aggregation skips symlinks and regular-file bytes only are counted.
- Recursive aggregation is bounded by a hard entry cap and a soft time cap to keep the UI responsive.
- `POST /api/v4/files/stat` uses the same handler and behavior.

Errors:
- `400 bad_request` for symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the path does not exist
- `500 server_error` when metadata or directory traversal fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/stat`

Purpose:
Return metadata for a user-visible file, directory, or storage root.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
Same as `GET /api/v4/files/stat`.

Response:
Same as `GET /api/v4/files/stat`.

Notes:
This route is registered to the same server-side handler as `GET /api/v4/files/stat`.

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/stat_sel`

Purpose:
Return aggregate metadata for a selected set of user-visible files and directories.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
JSON body:

- `paths`: array of user-relative paths. `.`, `./`, or `/` means the authenticated user's storage root.

Validation:
- request body must be valid JSON
- body must be an object containing `paths` as an array
- each item in `paths` must be a string
- each path is resolved through the metadata-aware logical item resolver
- symlinks are rejected
- selection processing is capped at 200 input paths

Response:
`200 OK` JSON:

- `ok`: `true`
- `count`: number of successfully returned item stats
- `files`: number of selected files counted
- `dirs`: number of selected directories counted
- `other`: number of other selected item types
- `bytes_total`: aggregate bytes for files plus recursive directory bytes
- `partial`: boolean indicating whether some inputs were skipped, incomplete, unsupported, or capped
- `limits`: server-side limits:
  - `max_items`
  - `scan_cap`
  - `time_cap_ms`
- `items`: array of per-item stats
- `errors`: array of per-path errors

Per-item fields include:

- `path`
- `path_norm`
- `type`
- `mode_octal`
- `bytes` for files
- `bytes_recursive`, `recursive_scanned_entries`, and `recursive_complete` for directories

Notes:
- Directory byte aggregation uses the same bounded recursive scan behavior as `/api/v4/files/stat`.
- The route returns `200 OK` with `partial: true` when some selected paths fail while others succeed.

Errors:
- `400 bad_request` for invalid JSON or invalid body shape
- `403 storage_unallocated` when the authenticated user has no allocated storage

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/touch`

Purpose:
Create a new empty file under the authenticated user's storage.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file path

Validation:
- `path` must be present
- path is resolved with strict user path containment rules
- parent directories are created as needed
- parent directory chain is checked before and after creation so symlinks are not used
- target must not already exist
- symlink targets are rejected

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested user-relative path
- `action`: `created`

Errors:
- `400 bad_request` for missing path, invalid path, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `409 already_exists` when the file already exists
- `409 path_conflict` when the parent path is unsafe
- `500 server_error` when parent creation, target inspection, or file creation fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/tree`

Purpose:
Return a bounded tree representation for a file or directory under the authenticated user's storage.

Auth:
User session. Requires the authenticated user to have allocated storage.

Request:
Query parameters:

- `path`: optional user-relative base path. Defaults to `.` for the user's storage root.
- `max`: optional maximum entry count. Defaults to `500` and is clamped to the range `1..5000`.

Validation:
- base path is resolved with strict user path containment rules
- symlinked base paths are rejected
- target path must exist
- symlink children are skipped and not followed

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: requested base path
- `max`: effective max entry count
- `truncated`: boolean indicating whether output was capped
- `entries`: number of entries counted, including the root node
- `files`: number of file nodes counted
- `dirs`: number of directory nodes counted
- `tree`: root node object

Tree node fields:

- `name`: display name
- `path`: user-relative path
- `type`: `file`, `dir`, or `other`
- `bytes`: file size for regular files
- `children`: array for directory nodes

Errors:
- `400 bad_request` for invalid path or symlinked base path
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the base path does not exist

Source:
`server/src/routes/routes_files_core.inc`

---

### GET `/api/v4/files/versions/archive_manifest`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41136`

---

### GET `/api/v4/files/versions/blob`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41202`

---

### POST `/api/v4/files/versions/delete`

Purpose:
Delete one stored file version.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
JSON body:

- `path`: required user-relative path
- `version_id`: required version id

Validation and behavior:
- requires same-origin before auth/body processing
- validates JSON body
- validates and normalizes `path`
- checks that the user's storage is allocated
- deletes exactly one stored version blob/index record
- returns deletion counters

Response:
`200 OK` JSON:

- `ok`
- `scope_type`
- `scope_id`
- `path`
- `version_id`
- `versions_deleted`
- `version_bytes_deleted`
- `version_blobs_missing`

Errors:
- `400 bad_request`
- `403 storage_unallocated`
- `403 forbidden` / origin mismatch
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_file_versions_manage.cpp`

---

### GET `/api/v4/files/versions/download`

Purpose:
Download one stored file version blob.

Auth:
User session. Requires allocated user storage. This is a read/download route and does not require same-origin mutation protection.

Request:
Query parameters:

- `path`: required user-relative path
- `version_id`: required version id

Validation and behavior:
- validates and normalizes `path`
- checks that the user's storage is allocated
- resolves the requested version blob
- returns the blob as `application/octet-stream`
- sets `Content-Disposition`, `X-PQNAS-Version-Id`, and `X-PQNAS-SHA256`

Response:
`200 OK` binary body.

Errors:
- `400 bad_request`
- `403 storage_unallocated`
- `404 not_found`
- `415 unsupported`
- `500 server_error`

Source:
`server/src/routes/routes_file_versions_read.cpp`

---

### POST `/api/v4/files/versions/flag`

Purpose:
Flag/bookmark one stored file version for the authenticated user.

Auth:
User session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `path`: required user-relative path
- `version_id`: required version id
- `note`: optional note

Validation and behavior:
- requires same-origin before auth/body processing
- validates JSON body
- validates and normalizes `path`
- flags the version for the authenticated user
- returns updated flag count and whether the authenticated user has flagged the version

Response:
`200 OK` JSON:

- `ok`
- `flagged`
- `flag_count`
- `flagged_by_me`

Errors:
- `400 bad_request`
- `403 forbidden` / origin mismatch
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_file_versions_manage.cpp`

---

### GET `/api/v4/files/versions/list`

Purpose:
List stored versions for one user-visible file path.

Auth:
User session. Requires allocated user storage indirectly through the version scope. This is a read route and does not require same-origin mutation protection.

Request:
Query parameters:

- `path`: required user-relative path
- `limit`: optional maximum number of versions. Default is `100`, clamped to `500`.

Validation and behavior:
- validates and normalizes `path`
- lists versions for scope `user` and the authenticated user's fingerprint
- includes flag metadata for each returned version
- does not return version blob contents

Response:
`200 OK` JSON:

- `ok`
- `scope_type`
- `scope_id`
- `path`
- `versions[]`

Errors:
- `400 bad_request` for missing or invalid path
- `500 server_error` when version listing fails

Source:
`server/src/routes/routes_file_versions_read.cpp`

---

### GET `/api/v4/files/versions/read_text`

Purpose:
Read one stored file version as UTF-8 text.

Auth:
User session. Requires allocated user storage. This is a read route and does not require same-origin mutation protection.

Request:
Query parameters:

- `path`: required user-relative path
- `version_id`: required version id

Validation and behavior:
- validates and normalizes `path`
- checks that the user's storage is allocated
- resolves the requested version blob
- reads at most the route's text-read cap
- rejects unsupported, missing, too-large, or non-text/invalid content through the version read helper

Response:
`200 OK` JSON:

- `ok`
- `scope_type`
- `scope_id`
- `path`
- `version_id`
- `created_at`
- `bytes`
- `sha256` / `sha256_hex`
- `encoding`
- `had_utf8_bom`
- `text`

Errors:
- `400 bad_request`
- `403 storage_unallocated`
- `404 not_found`
- `413 too_large`
- `415 unsupported`
- `500 server_error`

Source:
`server/src/routes/routes_file_versions_read.cpp`

---

### GET `/api/v4/files/versions/summary`

Purpose:
Return aggregate version statistics for the authenticated user's file-version scope.

Auth:
User session. This is a read route and does not require same-origin mutation protection.

Request:
No required query parameters.

Response:
`200 OK` JSON:

- `ok`
- `scope_type`
- `scope_id`
- `versions_count`
- `versions_bytes`

Errors:
- `500 server_error` when summary generation fails

Source:
`server/src/routes/routes_file_versions_manage.cpp`

---

### POST `/api/v4/files/versions/unflag`

Purpose:
Remove the authenticated user's flag/bookmark from one stored file version.

Auth:
User session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `path`: required user-relative path
- `version_id`: required version id

Validation and behavior:
- requires same-origin before auth/body processing
- validates JSON body
- validates and normalizes `path`
- removes the authenticated user's flag from the version
- returns updated flag count and whether the authenticated user has flagged the version

Response:
`200 OK` JSON:

- `ok`
- `flagged`
- `flag_count`
- `flagged_by_me`

Errors:
- `400 bad_request`
- `403 forbidden` / origin mismatch
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_file_versions_manage.cpp`

---

### POST `/api/v4/files/write_text`

Purpose:
Overwrite an existing text file using JSON input with optional optimistic concurrency checks.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
JSON body:

- `path`: required user-relative existing file path
- `text`: required/optional text value; defaults to empty string
- `expected_mtime_epoch`: optional previous modification timestamp
- `expected_sha256`: optional previous SHA-256 digest

Validation:
- body must be valid JSON object
- `path` must be present
- `text` must be valid UTF-8
- text size must not exceed browser text-edit limit
- target must resolve to an existing regular file
- symlinks are rejected
- existing file must look like text
- optional mtime/SHA-256 checks must still match current file state
- quota is checked because text edits can increase file size
- target is rechecked immediately before writing

Behavior:
- Uses atomic UTF-8 text write helper.
- Computes SHA-256 before and after the write.
- Rejects stale edits with `changed_on_server`.

Response:
`200 OK` JSON:

- `ok`: `true`
- `path`: normalized user-relative path
- `bytes`: new file size
- `mtime_epoch`: new modification timestamp
- `sha256`: new SHA-256 digest

Errors:
- `400 bad_request` for invalid JSON, missing path, invalid UTF-8, invalid path, or symlink use
- `400 not_text` when the file does not look like text
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the file does not exist
- `409 changed_on_server` when optimistic concurrency checks fail
- `409 path_conflict` when target changes before write
- `413 too_large` when input or existing file is too large for browser editing
- `413 quota_exceeded` when write would exceed quota
- `500 server_error` when hashing, quota checking, writing, or post-write hashing fails

Source:
`server/src/routes/routes_files_core.inc`

---

### GET `/api/v4/files/zip`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:34058`

---

### POST `/api/v4/files/zip`

Purpose:
Build and return a zip archive for a single selected file or directory.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
Query parameters:

- `path`: required user-relative file or directory path
- `max_bytes`: optional input byte limit. Defaults to 50 MiB and is clamped to 1..250 MiB.

Validation:
- `path` must be present
- leading `-` paths are rejected before calling `zip`
- path must resolve under the user's storage
- target must exist and be a regular file or directory
- symlinks are rejected at the selected path and inside directory trees
- selected content must not exceed `max_bytes`
- zip output is bounded by `max_bytes` plus overhead

Response:
`200 OK` binary response:

- `Content-Type: application/zip`
- `Content-Disposition: attachment; filename=...`
- `Cache-Control: no-store`

Behavior:
- Pre-walks the selected content to count files/directories, input bytes, and reject symlinks.
- Runs `/usr/bin/zip` via `execvp`.
- Captures zip output in memory with a bounded limit.

Errors:
- `400 bad_request` for missing path, invalid path, unsupported type, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when the selected path does not exist
- `413 too_large` when input or zip output exceeds limits
- `500 server_error` when walking, pipe/fork/exec/read, or zip execution fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/files/zip_sel`

Purpose:
Build and return a zip archive for multiple selected files/directories.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
JSON body:

- `paths`: required array of user-relative file or directory paths
- `max_bytes`: optional input byte limit. Defaults to 50 MiB and is clamped to 1..250 MiB.
- `base`: optional user-relative base directory used to make zip entries relative to the current folder

Validation:
- body must be valid JSON object
- `paths` must be an array
- path entries are normalized, deduplicated, and sanitized
- empty, traversal, CR/LF, and leading `-` paths are rejected
- selection count is capped at 500 paths
- children are dropped when their parent directory is already selected
- optional `base` must resolve to an existing directory
- selected paths must be inside `base` when `base` is provided
- all selected paths must resolve under the user's storage
- symlinks are rejected at selected paths and inside directory trees
- selected content must not exceed `max_bytes`
- zip output is bounded by `max_bytes` plus overhead

Response:
`200 OK` binary response:

- `Content-Type: application/zip`
- `Content-Disposition: attachment; filename="selection.zip"`
- `Cache-Control: no-store`

Behavior:
- Pre-walks all selections to count files/directories, input bytes, and reject symlinks.
- Runs `/usr/bin/zip` via `execvp` using `zip -r -q - -@`.
- Feeds selected paths to zip over stdin.
- Captures zip output in memory with a bounded limit.

Errors:
- `400 bad_request` for invalid JSON, missing paths, invalid base, invalid path, path outside base, unsupported type, or symlink use
- `403 storage_unallocated` when the authenticated user has no allocated storage
- `404 not_found` when a selected path does not exist
- `413 too_large` when there are too many selected paths, input exceeds limits, or zip output exceeds limits
- `500 server_error` when walking, pipe/fork/stdin/write/read, or zip execution fails

Source:
`server/src/routes/routes_files_core.inc`

---

### POST `/api/v4/gallery/export_sel_zip`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:47131`

---

### GET `/api/v4/gallery/list`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:34993`

---

### POST `/api/v4/gallery/meta/embedded_get`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:49072`

---

### POST `/api/v4/gallery/meta/get`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:39694`

---

### POST `/api/v4/gallery/meta/set`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:39225`

---

### GET `/api/v4/gallery/search`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:36636`

---

### GET `/api/v4/gallery/thumb`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:38653`

---

### GET `/api/v4/gallery/tree_stats`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:36516`

---

### GET `/api/v4/me`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:20509`

---

### GET `/api/v4/me/storage`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:33050`

---

### GET `/api/v4/music/cover`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:35570`

---

### GET `/api/v4/photogallery/stats`

Purpose:
Gallery/photo related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:39973`

---

### POST `/api/v4/poolmgr/add-slot`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:13189`

---

### POST `/api/v4/poolmgr/apply-layout`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:13681`

---

### POST `/api/v4/poolmgr/plan-layout`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:13518`

---

### POST `/api/v4/poolmgr/remove-slot`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:13277`

---

### POST `/api/v4/poolmgr/set-layout`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:13391`

---

### GET `/api/v4/raid/balance-status`

Purpose:
Health/status endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:14224`

---

### GET `/api/v4/raid/discovery`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:14036`

---

### GET `/api/v4/raid/exec-record`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:13965`

---

### GET `/api/v4/raid/exec-record`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18489`

---

### POST `/api/v4/raid/execute/add-device`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:16642`

---

### POST `/api/v4/raid/execute/convert-mode`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:15858`

---

### POST `/api/v4/raid/execute/create-pool`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18020`

---

### POST `/api/v4/raid/execute/destroy-pool`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:17190`

---

### POST `/api/v4/raid/execute/remove-device`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:17538`

---

### POST `/api/v4/raid/execute/scrub`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:14650`

---

### GET `/api/v4/raid/health`

Purpose:
Health/status endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18587`

---

### GET `/api/v4/raid/job`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18460`

---

### POST `/api/v4/raid/plan/add-device`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:15307`

---

### POST `/api/v4/raid/plan/convert-mode`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:15645`

---

### POST `/api/v4/raid/plan/create-pool`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:16530`

---

### POST `/api/v4/raid/plan/remove-device`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:16219`

---

### POST `/api/v4/raid/plan/scrub`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:14506`

---

### GET `/api/v4/raid/scrub-status`

Purpose:
Health/status endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:14378`

---

### GET `/api/v4/raid/status`

Purpose:
Health/status endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:15108`

---

### GET `/api/v4/reelstack/index`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:37179`

---

### GET `/api/v4/reelstack/meta`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:37956`

---

### POST `/api/v4/reelstack/meta/set`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:38523`

---

### POST `/api/v4/reelstack/scan`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:37249`

---

### GET `/api/v4/reelstack/thumb`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:37609`

---

### GET `/api/v4/reelstack/user_meta`

Purpose:
User management or user profile related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:38464`

---

### POST `/api/v4/shares/create`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:46015`

---

### GET `/api/v4/shares/list`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:46625`

---

### POST `/api/v4/shares/pq/enroll`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:44754`

---

### POST `/api/v4/shares/pq/open`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:45567`

---

### GET `/api/v4/shares/pq/open/chunk`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:45439`

---

### POST `/api/v4/shares/pq/open/init`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:45262`

---

### POST `/api/v4/shares/pq/recipient/update`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:45768`

---

### POST `/api/v4/shares/revoke`

Purpose:
File sharing or public link endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:46515`

---

### POST `/api/v4/snapshots/create`

Purpose:
Create a read-only Btrfs snapshot for one configured snapshot volume.

Auth:
Admin session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `volume`: required snapshot volume name
- `id`: optional snapshot id. If omitted, a `MANUAL_<utc>` id is generated.

Validation and behavior:
- requires admin authentication
- requires same-origin before mutation
- validates volume against snapshot settings
- requires snapshots to be enabled for the volume
- only allows live source paths under `/srv/pqnas/`
- only allows legacy global snapshot roots or pool-local `.snapshots` roots
- validates snapshot id character set
- creates snapshot root if needed
- runs `sudo -n /usr/bin/btrfs subvolume snapshot -r`
- probes whether the created target is a Btrfs subvolume
- writes audit event

Response:
`200 OK` JSON:

- `ok`
- `volume`
- `id`
- `path`
- `is_btrfs_subvolume`
- `probe_detail`

Errors:
- `400 bad_request`
- `403 no_privs`
- `404 not_found`
- `409 disabled`
- `409 already_exists`
- `500 server_error`

Source:
`server/src/routes/routes_snapshots_create.cpp`

---

### GET `/api/v4/snapshots/info`

Purpose:
Return Btrfs details for one snapshot.

Auth:
Admin session. This is a read route.

Request:
Query parameters:

- `volume`: required snapshot volume name
- `id`: required snapshot id

Validation and behavior:
- requires admin authentication
- validates volume against snapshot settings
- resolves snapshot path under the configured snapshot root
- requires snapshot path to exist
- runs Btrfs subvolume show through the configured helper

Response:
`200 OK` JSON:

- `ok`
- `volume`
- `id`
- `snapshot_path`
- `btrfs_show_ok`
- `btrfs_show_rc`
- `btrfs_show`
- `hint`

Errors:
- `400 bad_request`
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_snapshots_browse.cpp`

---

### GET `/api/v4/snapshots/list`

Purpose:
List snapshots for one configured snapshot volume.

Auth:
Admin session. This is a read route.

Request:
Query parameters:

- `volume`: required snapshot volume name

Validation and behavior:
- requires admin authentication
- validates volume against snapshot settings
- requires snapshot root to exist
- scans snapshot root for directory entries
- probes whether each entry is a Btrfs subvolume
- sorts newest entries first

Response:
`200 OK` JSON:

- `ok`
- `volume`
- `snap_root`
- `snapshots[]`

Errors:
- `400 bad_request`
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_snapshots_browse.cpp`

---

### POST `/api/v4/snapshots/restore/confirm`

Purpose:
Confirm and start a prepared snapshot restore job.

Auth:
Admin session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `confirm_id`: required id returned by restore prepare
- `confirm_text`: required exact confirmation phrase

Validation and behavior:
- requires admin authentication
- requires same-origin before mutation
- validates confirmation id and exact confirmation phrase
- removes the confirmation plan once used
- revalidates snapshot path and Btrfs subvolume status
- writes a restore job JSON under `/run/pqnas/restore`
- starts `pqnas-restore@<job_id>.service`
- writes audit event

Response:
`200 OK` JSON:

- `ok`
- `job_id`
- `volume`
- `id`

Errors:
- `400 bad_request`
- `404 not_found`
- `500 restore_start_failed` / `server_error`

Source:
`server/src/routes/routes_snapshots_restore.cpp`

---

### POST `/api/v4/snapshots/restore/prepare`

Purpose:
Prepare a high-impact snapshot restore plan and return a confirmation challenge.

Auth:
Admin session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `volume`: required snapshot volume name
- `id`: required snapshot id
- `mode`: currently must be `swap`
- `force_stop`: must be `true` in v1

Validation and behavior:
- requires admin authentication
- requires same-origin before mutation
- validates volume and snapshot id
- requires snapshot to be a Btrfs subvolume
- creates an in-memory restore confirmation plan with short expiry
- returns required confirmation phrase and planned restore steps
- warns that restore replaces live volume content and requires downtime

Response:
`200 OK` JSON:

- `ok`
- `confirm_id`
- `expires_in_sec`
- `plan`

Errors:
- `400 bad_request`
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_snapshots_restore.cpp`

---

### GET `/api/v4/snapshots/restore/status`

Purpose:
Read the status or result of a snapshot restore job.

Auth:
Admin session. This is a read/status route.

Request:
Query parameters:

- `job_id`: required restore job id beginning with `RJOB_`

Validation and behavior:
- requires admin authentication
- validates `job_id` prefix
- reads result JSON from `/run/pqnas/restore/<job_id>.result.json` when available
- otherwise queries the corresponding systemd unit status
- reports queued/running/done/failed-style status

Response:
`200 OK` JSON on known job.

Errors:
- `400 bad_request`
- `404 not_found`
- `500 server_error`

Source:
`server/src/routes/routes_snapshots_restore.cpp`

---

### GET `/api/v4/snapshots/volumes`

Purpose:
List configured snapshot volumes.

Auth:
Admin session. This is a read route.

Request:
No required query parameters.

Validation and behavior:
- requires admin authentication
- loads snapshot volume settings
- returns configured volume names, live source subvolume paths, snapshot roots, enabled flags, backend, and runtime user

Response:
`200 OK` JSON:

- `ok`
- `backend`
- `volumes[]`
- `runtime_user`

Errors:
- `500 server_error` when snapshot settings cannot be loaded

Source:
`server/src/routes/routes_snapshots_browse.cpp`

---

### GET `/api/v4/storage/disks`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12243`

---

### GET `/api/v4/storage/overview`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:13835`

---

### GET `/api/v4/storage/pools`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12376`

---

### POST `/api/v4/storage/pools/rename`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12936`

---

### POST `/api/v4/storage/pools/set-name`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12754`

---

### GET `/api/v4/storage/status`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12269`

---

### GET `/api/v4/system`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11853`

---

### GET `/api/v4/system/drives`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23979`

---

### POST `/api/v4/system/drives/refresh-now`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:24094`

---

### POST `/api/v4/system/drives/selftest/start`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:24114`

---

### GET `/api/v4/system/storage`

Purpose:
Storage, pool, or drive management endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23949`

---

### POST `/api/v4/uploads/cancel`

Purpose:
Cancel a chunked upload session and remove its temporary files.

Auth:
User session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `upload_id`: required upload session id

Validation and behavior:
- `upload_id` format is validated
- session directory for the authenticated user is removed recursively

Response:
`200 OK` JSON:

- `ok`: `true`
- `upload_id`
- `removed_entries`: number of removed filesystem entries

Errors:
- `400 bad_request` for invalid upload id
- `500 server_error` when removing the upload session fails

Source:
`server/src/routes/routes_uploads_chunked.cpp`

---

### PUT `/api/v4/uploads/chunk`

Purpose:
Upload one raw chunk for an existing chunked upload session.

Auth:
User session. Requires same-origin cookie mutation protection.

Request:
Query parameters:

- `upload_id`: required upload session id
- `index`: required zero-based chunk index

Body:
Raw chunk bytes streamed from the request body.

Required headers:

- `Content-Length`: required and must match the expected chunk size for this index

Validation and behavior:
- `upload_id` format is validated
- chunk index must be valid for the session
- session metadata must exist and be internally consistent
- stored destination path in session metadata must normalize exactly
- `Content-Length` must equal expected chunk byte count
- temporary upload-session disk pressure is checked against user quota
- chunk is written to a temporary file and renamed into place
- per-chunk size is fixed by the session chunk size, normally 16 MiB except possibly the last chunk

Response:
`200 OK` JSON:

- `ok`: `true`
- `upload_id`
- `index`
- `bytes`: written chunk bytes

Errors:
- `400 bad_request` for invalid upload id, missing/invalid index, invalid session metadata, invalid session path, or chunk size mismatch
- `403 quota_exceeded` or quota policy denial when temporary chunk storage would exceed quota
- `404 not_found` when the upload session does not exist
- `411 length_required` when `Content-Length` is missing
- `500 server_error` when preparing the chunk directory, writing the chunk, or renaming the chunk fails

Source:
`server/src/routes/routes_uploads_chunked.cpp`

---

### POST `/api/v4/uploads/finish`

Purpose:
Assemble uploaded chunks and commit the completed file into user storage.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
JSON body:

- `upload_id`: required upload session id

Validation and behavior:
- upload session metadata must exist and be internally consistent
- destination path in metadata must normalize exactly
- all expected chunks must exist, must not be symlinks, and must have the expected byte sizes
- live write locks are checked before final commit
- destination is preflighted again before writing
- destination parent is checked before and after directory creation to avoid symlink races
- chunks are assembled into a temporary file in the destination directory
- overwrite mode preserves the previous live file version before replacing it
- final file is renamed into place
- file location metadata is updated
- gallery file facts are updated best-effort
- storage tiering landing metadata is recorded when tiering is enabled
- old physical file is cleaned up after successful overwrite when needed
- upload session directory is removed after successful finish
- file upload activity is recorded best-effort

Response:
`200 OK` JSON:

- `ok`: `true`
- `chunked`: `true`
- `fingerprint_hex`: authenticated user's fingerprint
- `path`: normalized user-relative path
- `bytes`: assembled file size
- `overwrite`: whether overwrite mode was enabled

Errors:
- `400 bad_request` for invalid JSON, invalid upload id, bad session metadata, invalid path, missing chunks, wrong chunk sizes, or symlink use
- `403 storage_unallocated` when storage is not allocated
- `403 quota_exceeded` or quota policy denial when final file would exceed quota
- `404 not_found` when the upload session does not exist
- `409 file_exists` when destination exists and overwrite is not enabled
- `409 path_conflict` when destination is unsafe or has changed unexpectedly
- `500 server_error` when preserving a version, assembling chunks, creating directories, renaming, updating metadata, or committing fails

Source:
`server/src/routes/routes_uploads_chunked.cpp`

---

### POST `/api/v4/uploads/start`

Purpose:
Start a chunked upload session for a user-storage file.

Auth:
User session. Requires same-origin cookie mutation protection and allocated user storage.

Request:
JSON body:

- `path`: required user-relative destination file path
- `size_bytes`: required total upload size
- `overwrite`: optional boolean. Defaults to `false`.

Validation and behavior:
- total upload size is capped at 64 GiB
- destination path is normalized with strict user-relative path rules
- live write locks are checked for the destination
- quota, target conflicts, metadata conflicts, symlink-safe destination parent, and storage tiering target are preflighted before session creation
- creates an upload session under the server upload sessions directory
- chunk size is fixed to 16 MiB

Response:
`200 OK` JSON:

- `ok`: `true`
- `upload_id`
- `path`: normalized user-relative destination path
- `size_bytes`
- `chunk_size`
- `chunks_total`

Errors:
- `400 bad_request` for invalid JSON, missing path, missing/invalid size, or invalid path
- `403 storage_unallocated` when storage is not allocated
- `403 quota_exceeded` or quota policy denial when upload would exceed quota
- `409 file_exists` when destination exists and overwrite is not enabled
- `409 path_conflict` when destination is unsafe or conflicts with an existing logical directory/file ancestor
- `413 upload_too_large` when total upload size exceeds the session cap
- `500 server_error` when metadata lookup, tiering target resolution, session directory creation, or metadata write fails

Source:
`server/src/routes/routes_uploads_chunked.cpp`

---

### GET `/api/v4/user/profile`

Purpose:
User management or user profile related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:20635`

---

### POST `/api/v4/user/profile/avatar_remove`

Purpose:
User management or user profile related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43626`

---

### POST `/api/v4/user/profile/avatar_upload`

Purpose:
User management or user profile related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43465`

---

### POST `/api/v4/user/profile/update`

Purpose:
User management or user profile related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:20679`

---

### GET `/api/v4/users/avatar`

Purpose:
User management or user profile related endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:43341`

---

### POST `/api/v5/verify`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21152`

---

### GET `/app`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12200`

---

### GET `/app`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12210`

---

### GET `/apps/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/(.*)`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11871`

---

### GET `/pq/invite/([A-Za-z0-9_-]+)`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:49368`

---

### GET `/s/([A-Za-z0-9_-]+)`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:48350`

---

### GET `/static/(.+)`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23063`

---

### GET `/static/admin.js`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11965`

---

### GET `/static/admin_approvals.js`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23053`

---

### GET `/static/admin_apps.js`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21252`

---

### GET `/static/admin_audit.js`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12164`

---

### GET `/static/admin_badges.js`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21304`

---

### GET `/static/admin_settings.js`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:18936`

---

### GET `/static/admin_stats.js`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:22052`

---

### GET `/static/admin_users.js`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21275`

---

### GET `/static/app.js`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11959`

---

### GET `/static/pqnas_v5.js`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12178`

---

### GET `/static/system.js`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:11922`

---

### GET `/static/theme.css`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21282`

---

### GET `/static/theme.js`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21293`

---

### GET `/static/wait_approval.js`

Purpose:
TODO: describe purpose.

Auth:
Public/static

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21199`

---

### GET `/success`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:12192`

---

### GET `/system`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23930`

---

### GET `/wait-approval`

Purpose:
TODO: describe purpose.

Auth:
Unknown

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:21191`

---

### GET `/api/v4/trash/list`

Purpose:
List active or historical trash entries for the authenticated user's own trash scope, or for a workspace trash scope.

Auth:
User session.

Request:
Query parameters:

- `scope`: optional. `user` by default. Use `workspace` for workspace trash.
- `workspace_id`: required when `scope=workspace`
- `include_inactive`: optional boolean. Defaults to `false`.
- `limit`: optional result limit. Defaults to `200` and is clamped to `1..500`.

Validation and behavior:
- authenticates the actor
- user scope lists only the authenticated user's trash
- workspace scope reloads workspace metadata and requires enabled workspace membership
- non-workspace `scope` values intentionally fall back to user scope
- returns public trash metadata and does not expose internal payload physical paths

Response:
`200 OK` JSON:

- `ok`
- `scope_type`
- `scope_id`
- `include_inactive`
- `items[]`

Errors:
- `400 bad_request` for missing `workspace_id` when `scope=workspace`
- `403 forbidden` when workspace access is denied
- `404 not_found` when workspace lookup fails
- `500 server_error` when trash index/listing fails

Source:
`server/src/trash_routes.cpp`

---

### POST `/api/v4/trash/restore`

Purpose:
Restore one active trash entry back into its live user or workspace root.

Auth:
User session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `trash_id`: required trash entry id
- `rename_if_conflict`: optional boolean. When true, restore may rename on destination conflict.

Validation and behavior:
- authenticates the actor
- requires same-origin before mutation
- loads the trash row by `trash_id`
- user-scope trash can only be restored by the owning user
- workspace-scope trash can only be restored by a write-capable workspace member
- resolves the restore destination under the live user/workspace root
- delegates race-safe restore state transition and filesystem coordination to `TrashService`
- records audit and activity events best-effort

Response:
`200 OK` JSON:

- `ok`
- `trash_id`
- `item_type`
- `original_rel_path`
- `restored_rel_path`
- `size_bytes`
- `file_count`
- `renamed`

Errors:
- `400 bad_request`
- `403 forbidden`
- `403 storage_unallocated`
- `404 not_found`
- `409 trash_inactive`
- `409 path_conflict`
- `500 server_error`

Source:
`server/src/trash_routes.cpp`

---

### POST `/api/v4/trash/purge`

Purpose:
Permanently purge one active trash entry.

Auth:
User session. Requires same-origin cookie mutation protection.

Request:
JSON body:

- `trash_id`: required trash entry id

Validation and behavior:
- authenticates the actor
- requires same-origin before destructive mutation
- loads the trash row by `trash_id`
- user-scope trash can only be purged by the owning user
- workspace-scope trash can only be purged by a write-capable workspace member
- delegates race-safe purge state transition and payload removal to `TrashService`
- also returns version cleanup counters when related file versions are deleted

Response:
`200 OK` JSON:

- `ok`
- `trash_id`
- `size_bytes`
- `file_count`
- `versions_deleted`
- `version_bytes_deleted`
- `version_blobs_missing`
- `version_cleanup_error`

Errors:
- `400 bad_request`
- `403 forbidden`
- `404 not_found`
- `409 trash_inactive`
- `409 path_conflict`
- `500 server_error`

Source:
`server/src/trash_routes.cpp`

---
