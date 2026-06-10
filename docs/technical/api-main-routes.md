# main.cpp API Route Inventory

Status: generated draft.

This document is an initial route inventory generated from source code.
Descriptions and auth classifications should be reviewed manually before treating this as authoritative API documentation.

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
| `POST` | `/api/v4/files/delete` | User session | `server/src/main.cpp:31921` |
| `POST` | `/api/v4/files/du` | User session | `server/src/main.cpp:31543` |
| `POST` | `/api/v4/files/exists` | User session | `server/src/main.cpp:33253` |
| `GET` | `/api/v4/files/favorites` | User session | `server/src/main.cpp:31757` |
| `POST` | `/api/v4/files/favorites/add` | User session | `server/src/main.cpp:31799` |
| `POST` | `/api/v4/files/favorites/remove` | User session | `server/src/main.cpp:31866` |
| `GET` | `/api/v4/files/get` | User session | `server/src/main.cpp:34770` |
| `POST` | `/api/v4/files/hash` | User session | `server/src/main.cpp:26635` |
| `GET` | `/api/v4/files/list` | User session | `server/src/main.cpp:32767` |
| `POST` | `/api/v4/files/mkdir` | User session | `server/src/main.cpp:26412` |
| `POST` | `/api/v4/files/move` | User session | `server/src/main.cpp:25519` |
| `GET` | `/api/v4/files/office_preview` | User session | `server/src/main.cpp:34502` |
| `PUT` | `/api/v4/files/put` | User session | `server/src/main.cpp:40222` |
| `GET` | `/api/v4/files/read_text` | User session | `server/src/main.cpp:28296` |
| `POST` | `/api/v4/files/restore_version` | User session | `server/src/main.cpp:41766` |
| `POST` | `/api/v4/files/rmdir` | User session | `server/src/main.cpp:26810` |
| `POST` | `/api/v4/files/rmrf` | User session | `server/src/main.cpp:29820` |
| `POST` | `/api/v4/files/save_text` | User session | `server/src/main.cpp:27834` |
| `POST` | `/api/v4/files/search` | User session | `server/src/main.cpp:30545` |
| `GET` | `/api/v4/files/stat` | User session | `server/src/main.cpp:31217` |
| `POST` | `/api/v4/files/stat` | User session | `server/src/main.cpp:31216` |
| `POST` | `/api/v4/files/stat_sel` | User session | `server/src/main.cpp:31222` |
| `POST` | `/api/v4/files/touch` | User session | `server/src/main.cpp:27384` |
| `POST` | `/api/v4/files/tree` | User session | `server/src/main.cpp:27125` |
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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31921`

---

### POST `/api/v4/files/du`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31543`

---

### POST `/api/v4/files/exists`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:33253`

---

### GET `/api/v4/files/favorites`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31757`

---

### POST `/api/v4/files/favorites/add`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31799`

---

### POST `/api/v4/files/favorites/remove`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31866`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:32767`

---

### POST `/api/v4/files/mkdir`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:26412`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:26810`

---

### POST `/api/v4/files/rmrf`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:29820`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31217`

---

### POST `/api/v4/files/stat`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31216`

---

### POST `/api/v4/files/stat_sel`

Purpose:
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:31222`

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
File operation endpoint.

Auth:
User session

Request:
TODO.

Response:
TODO.

Source:
`server/src/main.cpp:27125`

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

