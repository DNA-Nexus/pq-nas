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
| `POST` | `/api/v4/admin/storage/tiering/migrate_one` | Admin session | `server/src/main.cpp:47053` |
| `GET` | `/api/v4/admin/storage/tiering/status` | Admin session | `server/src/main.cpp:48088` |
| `GET` | `/api/v4/admin/users` | Admin session | `server/src/main.cpp:22978` |
| `GET` | `/api/v4/admin/users/avatar` | Admin session | `server/src/main.cpp:43323` |
| `POST` | `/api/v4/admin/users/avatar_remove` | Admin session | `server/src/main.cpp:43418` |
| `POST` | `/api/v4/admin/users/avatar_upload` | Admin session | `server/src/main.cpp:43215` |
| `POST` | `/api/v4/admin/users/cleanup_old_storage` | Admin session | `server/src/main.cpp:46926` |
| `GET` | `/api/v4/admin/users/cleanup_old_storage_status` | Admin session | `server/src/main.cpp:47020` |
| `POST` | `/api/v4/admin/users/delete` | Admin session | `server/src/main.cpp:44677` |
| `POST` | `/api/v4/admin/users/disable` | Admin session | `server/src/main.cpp:44583` |
| `POST` | `/api/v4/admin/users/enable` | Admin session | `server/src/main.cpp:43155` |
| `POST` | `/api/v4/admin/users/migrate_storage` | Admin session | `server/src/main.cpp:46775` |
| `GET` | `/api/v4/admin/users/migrate_storage_status` | Admin session | `server/src/main.cpp:46893` |
| `POST` | `/api/v4/admin/users/status` | Admin session | `server/src/main.cpp:23160` |
| `POST` | `/api/v4/admin/users/storage` | Admin session | `server/src/main.cpp:23262` |
| `GET` | `/api/v4/admin/users/storage_preview` | Admin session | `server/src/main.cpp:23762` |
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
| `POST` | `/api/v4/files/cat` | User session | `server/src/main.cpp:27641` |
| `POST` | `/api/v4/files/copy` | User session | `server/src/main.cpp:33411` |
| `POST` | `/api/v4/files/delete` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/du` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/exists` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/favorites` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/favorites/add` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/favorites/remove` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/get` | User session | `server/src/main.cpp:34770` |
| `POST` | `/api/v4/files/hash` | User session | `server/src/main.cpp:26635` |
| `GET` | `/api/v4/files/list` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/mkdir` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/move` | User session | `server/src/main.cpp:25519` |
| `GET` | `/api/v4/files/office_preview` | User session | `server/src/main.cpp:34502` |
| `PUT` | `/api/v4/files/put` | User session | `server/src/main.cpp:40222` |
| `GET` | `/api/v4/files/read_text` | User session | `server/src/main.cpp:28296` |
| `POST` | `/api/v4/files/restore_version` | User session | `server/src/main.cpp:41766` |
| `POST` | `/api/v4/files/rmdir` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/rmrf` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/save_text` | User session | `server/src/main.cpp:27834` |
| `POST` | `/api/v4/files/search` | User session | `server/src/main.cpp:30545` |
| `GET` | `/api/v4/files/stat` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/stat` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/stat_sel` | User session | `server/src/routes/routes_files_core.inc` |
| `POST` | `/api/v4/files/touch` | User session | `server/src/main.cpp:27384` |
| `POST` | `/api/v4/files/tree` | User session | `server/src/routes/routes_files_core.inc` |
| `GET` | `/api/v4/files/versions/archive_manifest` | User session | `server/src/main.cpp:41136` |
| `GET` | `/api/v4/files/versions/blob` | User session | `server/src/main.cpp:41202` |
| `POST` | `/api/v4/files/versions/delete` | User session | `server/src/main.cpp:41689` |
| `GET` | `/api/v4/files/versions/download` | User session | `server/src/main.cpp:41472` |
| `POST` | `/api/v4/files/versions/flag` | User session | `server/src/main.cpp:41653` |
| `GET` | `/api/v4/files/versions/list` | User session | `server/src/main.cpp:41291` |
| `GET` | `/api/v4/files/versions/read_text` | User session | `server/src/main.cpp:41382` |
| `GET` | `/api/v4/files/versions/summary` | User session | `server/src/main.cpp:41662` |
| `POST` | `/api/v4/files/versions/unflag` | User session | `server/src/main.cpp:41657` |
| `POST` | `/api/v4/files/write_text` | User session | `server/src/main.cpp:28520` |
| `GET` | `/api/v4/files/zip` | User session | `server/src/main.cpp:34058` |
| `POST` | `/api/v4/files/zip` | User session | `server/src/main.cpp:28886` |
| `POST` | `/api/v4/files/zip_sel` | User session | `server/src/main.cpp:29238` |
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
| `POST` | `/api/v4/snapshots/create` | User session | `server/src/main.cpp:41948` |
| `GET` | `/api/v4/snapshots/info` | User session | `server/src/main.cpp:42448` |
| `GET` | `/api/v4/snapshots/list` | User session | `server/src/main.cpp:42280` |
| `POST` | `/api/v4/snapshots/restore/confirm` | User session | `server/src/main.cpp:42785` |
| `POST` | `/api/v4/snapshots/restore/prepare` | User session | `server/src/main.cpp:42515` |
| `GET` | `/api/v4/snapshots/restore/status` | User session | `server/src/main.cpp:42647` |
| `GET` | `/api/v4/snapshots/volumes` | User session | `server/src/main.cpp:42220` |
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
| `POST` | `/api/v4/uploads/cancel` | User session | `server/src/main.cpp:25047` |
| `PUT` | `/api/v4/uploads/chunk` | User session | `server/src/main.cpp:24781` |
| `POST` | `/api/v4/uploads/finish` | User session | `server/src/main.cpp:25091` |
| `POST` | `/api/v4/uploads/start` | User session | `server/src/main.cpp:24619` |
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
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:47053`

---

### GET `/api/v4/admin/storage/tiering/status`

Purpose:
Admin management endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:48088`

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
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:46926`

---

### GET `/api/v4/admin/users/cleanup_old_storage_status`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:47020`

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
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:46775`

---

### GET `/api/v4/admin/users/migrate_storage_status`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:46893`

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
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23262`

---

### GET `/api/v4/admin/users/storage_preview`

Purpose:
User management or user profile related endpoint.

Auth:
Admin session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:23762`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:27641`

---

### POST `/api/v4/files/copy`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:33411`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:26635`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:25519`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:40222`

---

### GET `/api/v4/files/read_text`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:28296`

---

### POST `/api/v4/files/restore_version`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41766`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:27834`

---

### POST `/api/v4/files/search`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:30545`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:27384`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41689`

---

### GET `/api/v4/files/versions/download`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41472`

---

### POST `/api/v4/files/versions/flag`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41653`

---

### GET `/api/v4/files/versions/list`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41291`

---

### GET `/api/v4/files/versions/read_text`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41382`

---

### GET `/api/v4/files/versions/summary`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41662`

---

### POST `/api/v4/files/versions/unflag`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41657`

---

### POST `/api/v4/files/write_text`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:28520`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:28886`

---

### POST `/api/v4/files/zip_sel`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:29238`

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
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:41948`

---

### GET `/api/v4/snapshots/info`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:42448`

---

### GET `/api/v4/snapshots/list`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:42280`

---

### POST `/api/v4/snapshots/restore/confirm`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:42785`

---

### POST `/api/v4/snapshots/restore/prepare`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:42515`

---

### GET `/api/v4/snapshots/restore/status`

Purpose:
Health/status endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:42647`

---

### GET `/api/v4/snapshots/volumes`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:42220`

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
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:25047`

---

### PUT `/api/v4/uploads/chunk`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:24781`

---

### POST `/api/v4/uploads/finish`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:25091`

---

### POST `/api/v4/uploads/start`

Purpose:
TODO: describe purpose.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:24619`

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

