# main.cpp route map

- Source: `server/src/main.cpp`
- Total source lines: 49945
- Route registrations found: 190

## Route groups

| Count | Group |
|---:|---|
| 40 | `/api/v4/files` |
| 29 | `/api/v4/admin` |
| 19 | `/api/v4/raid` |
| 15 | `/static` |
| 8 | `/api/v4/gallery` |
| 8 | `/api/v4/shares` |
| 7 | `/admin` |
| 7 | `/api/v4/apps` |
| 7 | `/api/v4/snapshots` |
| 6 | `/api/v4/reelstack` |
| 6 | `/api/v4/storage` |
| 5 | `/api/v4/poolmgr` |
| 5 | `/api/v4/system` |
| 4 | `/api/v4/uploads` |
| 4 | `/api/v4/user` |
| 2 | `/api/v4/audit` |
| 2 | `/api/v4/me` |
| 2 | `/app` |
| 1 | `/` |
| 1 | `/api/debug/auth` |
| 1 | `/api/public/auth_mode` |
| 1 | `/api/public/gallery` |
| 1 | `/api/v4/music` |
| 1 | `/api/v4/photogallery` |
| 1 | `/api/v4/users` |
| 1 | `/api/v5/verify` |
| 1 | `/apps` |
| 1 | `/pq` |
| 1 | `/s` |
| 1 | `/success` |
| 1 | `/system` |
| 1 | `/wait-approval` |

## Routes

| Line | Method | Path |
|---:|---|---|
| 11848 | `GET` | `/api/public/auth_mode` |
| 11865 | `GET` | `/api/v4/system` |
| 11883 | `GET` | `/apps/([A-Za-z0-9_.-]+` |
| 11934 | `GET` | `/static/system.js` |
| 11947 | `GET` | `/admin/audit` |
| 11956 | `GET` | `/admin` |
| 11971 | `GET` | `/static/app.js` |
| 11977 | `GET` | `/static/admin.js` |
| 12058 | `GET` | `/api/v4/apps` |
| 12176 | `GET` | `/static/admin_audit.js` |
| 12190 | `GET` | `/static/pqnas_v5.js` |
| 12204 | `GET` | `/success` |
| 12212 | `GET` | `/app` |
| 12222 | `GET` | `/app` |
| 12232 | `GET` | `/` |
| 12247 | `GET` | `/api/v4/admin/ping` |
| 12255 | `GET` | `/api/v4/storage/disks` |
| 12281 | `GET` | `/api/v4/storage/status` |
| 12388 | `GET` | `/api/v4/storage/pools` |
| 12766 | `POST` | `/api/v4/storage/pools/set-name` |
| 12948 | `POST` | `/api/v4/storage/pools/rename` |
| 13201 | `POST` | `/api/v4/poolmgr/add-slot` |
| 13289 | `POST` | `/api/v4/poolmgr/remove-slot` |
| 13403 | `POST` | `/api/v4/poolmgr/set-layout` |
| 13530 | `POST` | `/api/v4/poolmgr/plan-layout` |
| 13693 | `POST` | `/api/v4/poolmgr/apply-layout` |
| 13847 | `GET` | `/api/v4/storage/overview` |
| 13977 | `GET` | `/api/v4/raid/exec-record` |
| 14048 | `GET` | `/api/v4/raid/discovery` |
| 14236 | `GET` | `/api/v4/raid/balance-status` |
| 14390 | `GET` | `/api/v4/raid/scrub-status` |
| 14518 | `POST` | `/api/v4/raid/plan/scrub` |
| 14662 | `POST` | `/api/v4/raid/execute/scrub` |
| 15120 | `GET` | `/api/v4/raid/status` |
| 15319 | `POST` | `/api/v4/raid/plan/add-device` |
| 15657 | `POST` | `/api/v4/raid/plan/convert-mode` |
| 15870 | `POST` | `/api/v4/raid/execute/convert-mode` |
| 16231 | `POST` | `/api/v4/raid/plan/remove-device` |
| 16542 | `POST` | `/api/v4/raid/plan/create-pool` |
| 16654 | `POST` | `/api/v4/raid/execute/add-device` |
| 17202 | `POST` | `/api/v4/raid/execute/destroy-pool` |
| 17550 | `POST` | `/api/v4/raid/execute/remove-device` |
| 18032 | `POST` | `/api/v4/raid/execute/create-pool` |
| 18472 | `GET` | `/api/v4/raid/job` |
| 18501 | `GET` | `/api/v4/raid/exec-record` |
| 18599 | `GET` | `/api/v4/raid/health` |
| 18734 | `GET` | `/api/v4/audit/tail` |
| 18765 | `GET` | `/api/v4/audit/verify` |
| 18796 | `GET` | `/admin/settings` |
| 18814 | `POST` | `/api/v4/admin/rotate-audit` |
| 18845 | `POST` | `/api/v4/admin/audit/preview-prune` |
| 18867 | `POST` | `/api/v4/admin/audit/prune` |
| 18948 | `GET` | `/static/admin_settings.js` |
| 18964 | `GET` | `/api/v4/admin/settings` |
| 19209 | `POST` | `/api/v4/admin/settings/send-dna-alert-contact-request` |
| 19283 | `POST` | `/api/v4/admin/settings` |
| 20351 | `POST` | `/api/v4/admin/settings/create-dna-alert-identity` |
| 20438 | `GET` | `/api/v4/admin/settings/dna-alert-identity-info` |
| 20448 | `POST` | `/api/v4/admin/settings/send-dna-alert-contact-request` |
| 20521 | `GET` | `/api/v4/me` |
| 20647 | `GET` | `/api/v4/user/profile` |
| 20691 | `POST` | `/api/v4/user/profile/update` |
| 21164 | `POST` | `/api/v5/verify` |
| 21203 | `GET` | `/wait-approval` |
| 21211 | `GET` | `/static/wait_approval.js` |
| 21218 | `GET` | `/admin/apps` |
| 21264 | `GET` | `/static/admin_apps.js` |
| 21277 | `GET` | `/admin/users` |
| 21287 | `GET` | `/static/admin_users.js` |
| 21294 | `GET` | `/static/theme.css` |
| 21305 | `GET` | `/static/theme.js` |
| 21316 | `GET` | `/static/admin_badges.js` |
| 22051 | `GET` | `/admin/stats` |
| 22064 | `GET` | `/static/admin_stats.js` |
| 22076 | `GET` | `/api/v4/admin/stats/trends` |
| 22675 | `GET` | `/api/v4/admin/stats/summary` |
| 22991 | `GET` | `/api/v4/admin/users` |
| 23056 | `GET` | `/admin/approvals` |
| 23066 | `GET` | `/static/admin_approvals.js` |
| 23076 | `GET` | `/static/(.+` |
| 23400 | `POST` | `/api/v4/admin/users/status` |
| 23540 | `POST` | `/api/v4/admin/users/storage` |
| 24040 | `GET` | `/api/v4/admin/users/storage_preview` |
| 24208 | `GET` | `/system` |
| 24227 | `GET` | `/api/v4/system/storage` |
| 24257 | `GET` | `/api/v4/system/drives` |
| 24372 | `POST` | `/api/v4/system/drives/refresh-now` |
| 24392 | `POST` | `/api/v4/system/drives/selftest/start` |
| 24897 | `POST` | `/api/v4/uploads/start` |
| 25059 | `PUT` | `/api/v4/uploads/chunk` |
| 25325 | `POST` | `/api/v4/uploads/cancel` |
| 25369 | `POST` | `/api/v4/uploads/finish` |
| 25797 | `POST` | `/api/v4/files/move` |
| 26690 | `POST` | `/api/v4/files/mkdir` |
| 26913 | `POST` | `/api/v4/files/hash` |
| 27088 | `POST` | `/api/v4/files/rmdir` |
| 27403 | `POST` | `/api/v4/files/tree` |
| 27662 | `POST` | `/api/v4/files/touch` |
| 27919 | `POST` | `/api/v4/files/cat` |
| 28112 | `POST` | `/api/v4/files/save_text` |
| 28574 | `GET` | `/api/v4/files/read_text` |
| 28798 | `POST` | `/api/v4/files/write_text` |
| 29164 | `POST` | `/api/v4/files/zip` |
| 29516 | `POST` | `/api/v4/files/zip_sel` |
| 30098 | `POST` | `/api/v4/files/rmrf` |
| 30823 | `POST` | `/api/v4/files/search` |
| 31494 | `POST` | `/api/v4/files/stat` |
| 31495 | `GET` | `/api/v4/files/stat` |
| 31500 | `POST` | `/api/v4/files/stat_sel` |
| 31821 | `POST` | `/api/v4/files/du` |
| 32035 | `GET` | `/api/v4/files/favorites` |
| 32077 | `POST` | `/api/v4/files/favorites/add` |
| 32144 | `POST` | `/api/v4/files/favorites/remove` |
| 32199 | `POST` | `/api/v4/files/delete` |
| 33045 | `GET` | `/api/v4/files/list` |
| 33328 | `GET` | `/api/v4/me/storage` |
| 33531 | `POST` | `/api/v4/files/exists` |
| 33689 | `POST` | `/api/v4/files/copy` |
| 34336 | `GET` | `/api/v4/files/zip` |
| 34780 | `GET` | `/api/v4/files/office_preview` |
| 35048 | `GET` | `/api/v4/files/get` |
| 35271 | `GET` | `/api/v4/gallery/list` |
| 35848 | `GET` | `/api/v4/music/cover` |
| 36794 | `GET` | `/api/v4/gallery/tree_stats` |
| 36914 | `GET` | `/api/v4/gallery/search` |
| 37457 | `GET` | `/api/v4/reelstack/index` |
| 37527 | `POST` | `/api/v4/reelstack/scan` |
| 37887 | `GET` | `/api/v4/reelstack/thumb` |
| 38234 | `GET` | `/api/v4/reelstack/meta` |
| 38742 | `GET` | `/api/v4/reelstack/user_meta` |
| 38801 | `POST` | `/api/v4/reelstack/meta/set` |
| 38931 | `GET` | `/api/v4/gallery/thumb` |
| 39503 | `POST` | `/api/v4/gallery/meta/set` |
| 39972 | `POST` | `/api/v4/gallery/meta/get` |
| 40251 | `GET` | `/api/v4/photogallery/stats` |
| 40500 | `PUT` | `/api/v4/files/put` |
| 41343 | `GET` | `/api/v4/files/archive_manifest` |
| 41414 | `GET` | `/api/v4/files/versions/archive_manifest` |
| 41480 | `GET` | `/api/v4/files/versions/blob` |
| 41569 | `GET` | `/api/v4/files/versions/list` |
| 41660 | `GET` | `/api/v4/files/versions/read_text` |
| 41750 | `GET` | `/api/v4/files/versions/download` |
| 41931 | `POST` | `/api/v4/files/versions/flag` |
| 41935 | `POST` | `/api/v4/files/versions/unflag` |
| 41940 | `GET` | `/api/v4/files/versions/summary` |
| 41967 | `POST` | `/api/v4/files/versions/delete` |
| 42044 | `POST` | `/api/v4/files/restore_version` |
| 42226 | `POST` | `/api/v4/snapshots/create` |
| 42569 | `GET` | `/api/v4/snapshots/volumes` |
| 42629 | `GET` | `/api/v4/snapshots/list` |
| 42797 | `GET` | `/api/v4/snapshots/info` |
| 42864 | `POST` | `/api/v4/snapshots/restore/prepare` |
| 42996 | `GET` | `/api/v4/snapshots/restore/status` |
| 43134 | `POST` | `/api/v4/snapshots/restore/confirm` |
| 43409 | `POST` | `/api/v4/admin/users/upsert` |
| 43528 | `POST` | `/api/v4/admin/users/enable` |
| 43588 | `POST` | `/api/v4/admin/users/avatar_upload` |
| 43696 | `GET` | `/api/v4/admin/users/avatar` |
| 43714 | `GET` | `/api/v4/users/avatar` |
| 43791 | `POST` | `/api/v4/admin/users/avatar_remove` |
| 43838 | `POST` | `/api/v4/user/profile/avatar_upload` |
| 43999 | `POST` | `/api/v4/user/profile/avatar_remove` |
| 44066 | `GET` | `/api/v4/apps/has` |
| 44204 | `GET` | `/api/v4/apps/list` |
| 44332 | `POST` | `/api/v4/apps/upload_install` |
| 44588 | `POST` | `/api/v4/apps/install_bundled` |
| 44758 | `POST` | `/api/v4/apps/launch_policy` |
| 44868 | `POST` | `/api/v4/apps/uninstall` |
| 44956 | `POST` | `/api/v4/admin/users/disable` |
| 45021 | `GET` | `/api/debug/auth/approvals` |
| 45050 | `POST` | `/api/v4/admin/users/delete` |
| 45127 | `POST` | `/api/v4/shares/pq/enroll` |
| 45635 | `POST` | `/api/v4/shares/pq/open/init` |
| 45812 | `GET` | `/api/v4/shares/pq/open/chunk` |
| 45940 | `POST` | `/api/v4/shares/pq/open` |
| 46141 | `POST` | `/api/v4/shares/pq/recipient/update` |
| 46388 | `POST` | `/api/v4/shares/create` |
| 46888 | `POST` | `/api/v4/shares/revoke` |
| 46998 | `GET` | `/api/v4/shares/list` |
| 47148 | `POST` | `/api/v4/admin/users/migrate_storage` |
| 47266 | `GET` | `/api/v4/admin/users/migrate_storage_status` |
| 47299 | `POST` | `/api/v4/admin/users/cleanup_old_storage` |
| 47393 | `GET` | `/api/v4/admin/users/cleanup_old_storage_status` |
| 47426 | `POST` | `/api/v4/admin/storage/tiering/migrate_one` |
| 47504 | `POST` | `/api/v4/gallery/export_sel_zip` |
| 48461 | `GET` | `/api/v4/admin/storage/tiering/status` |
| 48663 | `GET` | `/api/public/gallery/album/image` |
| 48723 | `GET` | `/s/([A-Za-z0-9_-]+` |
| 49445 | `POST` | `/api/v4/gallery/meta/embedded_get` |
| 49741 | `GET` | `/pq/invite/([A-Za-z0-9_-]+` |

## Raw route lines

### Line 11848: GET `/api/public/auth_mode`

```cpp
srv.Get("/api/public/auth_mode", [&](const httplib::Request& /*req*/, httplib::Response& res) {
```

### Line 11865: GET `/api/v4/system`

```cpp
srv.Get("/api/v4/system", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 11883: GET `/apps/([A-Za-z0-9_.-]+`

```cpp
srv.Get(R"(/apps/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/(.*))",
```

### Line 11934: GET `/static/system.js`

```cpp
srv.Get("/static/system.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 11947: GET `/admin/audit`

```cpp
srv.Get("/admin/audit", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 11956: GET `/admin`

```cpp
srv.Get("/admin", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 11971: GET `/static/app.js`

```cpp
srv.Get("/static/app.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 11977: GET `/static/admin.js`

```cpp
srv.Get("/static/admin.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 12058: GET `/api/v4/apps`

```cpp
srv.Get("/api/v4/apps", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 12176: GET `/static/admin_audit.js`

```cpp
srv.Get("/static/admin_audit.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 12190: GET `/static/pqnas_v5.js`

```cpp
srv.Get("/static/pqnas_v5.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 12204: GET `/success`

```cpp
srv.Get("/success", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 12212: GET `/app`

```cpp
srv.Get("/app", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 12222: GET `/app`

```cpp
srv.Get("/app", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 12232: GET `/`

```cpp
srv.Get("/", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 12247: GET `/api/v4/admin/ping`

```cpp
srv.Get("/api/v4/admin/ping", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 12255: GET `/api/v4/storage/disks`

```cpp
srv.Get("/api/v4/storage/disks", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 12281: GET `/api/v4/storage/status`

```cpp
srv.Get("/api/v4/storage/status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 12388: GET `/api/v4/storage/pools`

```cpp
srv.Get("/api/v4/storage/pools", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 12766: POST `/api/v4/storage/pools/set-name`

```cpp
srv.Post("/api/v4/storage/pools/set-name", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 12948: POST `/api/v4/storage/pools/rename`

```cpp
srv.Post("/api/v4/storage/pools/rename", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 13201: POST `/api/v4/poolmgr/add-slot`

```cpp
srv.Post("/api/v4/poolmgr/add-slot", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 13289: POST `/api/v4/poolmgr/remove-slot`

```cpp
srv.Post("/api/v4/poolmgr/remove-slot", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 13403: POST `/api/v4/poolmgr/set-layout`

```cpp
srv.Post("/api/v4/poolmgr/set-layout", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 13530: POST `/api/v4/poolmgr/plan-layout`

```cpp
srv.Post("/api/v4/poolmgr/plan-layout", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 13693: POST `/api/v4/poolmgr/apply-layout`

```cpp
srv.Post("/api/v4/poolmgr/apply-layout", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 13847: GET `/api/v4/storage/overview`

```cpp
srv.Get("/api/v4/storage/overview", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 13977: GET `/api/v4/raid/exec-record`

```cpp
srv.Get("/api/v4/raid/exec-record", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 14048: GET `/api/v4/raid/discovery`

```cpp
srv.Get("/api/v4/raid/discovery", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 14236: GET `/api/v4/raid/balance-status`

```cpp
srv.Get("/api/v4/raid/balance-status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 14390: GET `/api/v4/raid/scrub-status`

```cpp
srv.Get("/api/v4/raid/scrub-status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 14518: POST `/api/v4/raid/plan/scrub`

```cpp
srv.Post("/api/v4/raid/plan/scrub", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 14662: POST `/api/v4/raid/execute/scrub`

```cpp
srv.Post("/api/v4/raid/execute/scrub", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 15120: GET `/api/v4/raid/status`

```cpp
srv.Get("/api/v4/raid/status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 15319: POST `/api/v4/raid/plan/add-device`

```cpp
srv.Post("/api/v4/raid/plan/add-device", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 15657: POST `/api/v4/raid/plan/convert-mode`

```cpp
srv.Post("/api/v4/raid/plan/convert-mode", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 15870: POST `/api/v4/raid/execute/convert-mode`

```cpp
srv.Post("/api/v4/raid/execute/convert-mode", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 16231: POST `/api/v4/raid/plan/remove-device`

```cpp
srv.Post("/api/v4/raid/plan/remove-device", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 16542: POST `/api/v4/raid/plan/create-pool`

```cpp
srv.Post("/api/v4/raid/plan/create-pool", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 16654: POST `/api/v4/raid/execute/add-device`

```cpp
srv.Post("/api/v4/raid/execute/add-device", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 17202: POST `/api/v4/raid/execute/destroy-pool`

```cpp
srv.Post("/api/v4/raid/execute/destroy-pool", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 17550: POST `/api/v4/raid/execute/remove-device`

```cpp
srv.Post("/api/v4/raid/execute/remove-device", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18032: POST `/api/v4/raid/execute/create-pool`

```cpp
srv.Post("/api/v4/raid/execute/create-pool", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18472: GET `/api/v4/raid/job`

```cpp
srv.Get("/api/v4/raid/job", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18501: GET `/api/v4/raid/exec-record`

```cpp
srv.Get("/api/v4/raid/exec-record", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18599: GET `/api/v4/raid/health`

```cpp
srv.Get("/api/v4/raid/health", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18734: GET `/api/v4/audit/tail`

```cpp
srv.Get("/api/v4/audit/tail", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18765: GET `/api/v4/audit/verify`

```cpp
srv.Get("/api/v4/audit/verify", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18796: GET `/admin/settings`

```cpp
srv.Get("/admin/settings", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18814: POST `/api/v4/admin/rotate-audit`

```cpp
srv.Post("/api/v4/admin/rotate-audit", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18845: POST `/api/v4/admin/audit/preview-prune`

```cpp
srv.Post("/api/v4/admin/audit/preview-prune", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18867: POST `/api/v4/admin/audit/prune`

```cpp
srv.Post("/api/v4/admin/audit/prune", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 18948: GET `/static/admin_settings.js`

```cpp
srv.Get("/static/admin_settings.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 18964: GET `/api/v4/admin/settings`

```cpp
srv.Get("/api/v4/admin/settings", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 19209: POST `/api/v4/admin/settings/send-dna-alert-contact-request`

```cpp
srv.Post("/api/v4/admin/settings/send-dna-alert-contact-request", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 19283: POST `/api/v4/admin/settings`

```cpp
srv.Post("/api/v4/admin/settings", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 20351: POST `/api/v4/admin/settings/create-dna-alert-identity`

```cpp
srv.Post("/api/v4/admin/settings/create-dna-alert-identity", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 20438: GET `/api/v4/admin/settings/dna-alert-identity-info`

```cpp
srv.Get("/api/v4/admin/settings/dna-alert-identity-info", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 20448: POST `/api/v4/admin/settings/send-dna-alert-contact-request`

```cpp
srv.Post("/api/v4/admin/settings/send-dna-alert-contact-request", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 20521: GET `/api/v4/me`

```cpp
srv.Get("/api/v4/me", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 20647: GET `/api/v4/user/profile`

```cpp
srv.Get("/api/v4/user/profile", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 20691: POST `/api/v4/user/profile/update`

```cpp
srv.Post("/api/v4/user/profile/update", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 21164: POST `/api/v5/verify`

```cpp
srv.Post("/api/v5/verify", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 21203: GET `/wait-approval`

```cpp
srv.Get("/wait-approval", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 21211: GET `/static/wait_approval.js`

```cpp
srv.Get("/static/wait_approval.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 21218: GET `/admin/apps`

```cpp
srv.Get("/admin/apps", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 21264: GET `/static/admin_apps.js`

```cpp
srv.Get("/static/admin_apps.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 21277: GET `/admin/users`

```cpp
srv.Get("/admin/users", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 21287: GET `/static/admin_users.js`

```cpp
srv.Get("/static/admin_users.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 21294: GET `/static/theme.css`

```cpp
srv.Get("/static/theme.css", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 21305: GET `/static/theme.js`

```cpp
srv.Get("/static/theme.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 21316: GET `/static/admin_badges.js`

```cpp
srv.Get("/static/admin_badges.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 22051: GET `/admin/stats`

```cpp
srv.Get("/admin/stats", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 22064: GET `/static/admin_stats.js`

```cpp
srv.Get("/static/admin_stats.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 22076: GET `/api/v4/admin/stats/trends`

```cpp
srv.Get("/api/v4/admin/stats/trends", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 22675: GET `/api/v4/admin/stats/summary`

```cpp
srv.Get("/api/v4/admin/stats/summary", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 22991: GET `/api/v4/admin/users`

```cpp
srv.Get("/api/v4/admin/users", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 23056: GET `/admin/approvals`

```cpp
srv.Get("/admin/approvals", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 23066: GET `/static/admin_approvals.js`

```cpp
srv.Get("/static/admin_approvals.js", [&](const httplib::Request&, httplib::Response& res) {
```

### Line 23076: GET `/static/(.+`

```cpp
srv.Get(R"(/static/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 23400: POST `/api/v4/admin/users/status`

```cpp
srv.Post("/api/v4/admin/users/status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 23540: POST `/api/v4/admin/users/storage`

```cpp
srv.Post("/api/v4/admin/users/storage", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 24040: GET `/api/v4/admin/users/storage_preview`

```cpp
srv.Get("/api/v4/admin/users/storage_preview", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 24208: GET `/system`

```cpp
srv.Get("/system", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 24227: GET `/api/v4/system/storage`

```cpp
srv.Get("/api/v4/system/storage", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 24257: GET `/api/v4/system/drives`

```cpp
srv.Get("/api/v4/system/drives", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 24372: POST `/api/v4/system/drives/refresh-now`

```cpp
srv.Post("/api/v4/system/drives/refresh-now", [](const auto& /*req*/, auto& res) {
```

### Line 24392: POST `/api/v4/system/drives/selftest/start`

```cpp
srv.Post("/api/v4/system/drives/selftest/start", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 24897: POST `/api/v4/uploads/start`

```cpp
srv.Post("/api/v4/uploads/start", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 25059: PUT `/api/v4/uploads/chunk`

```cpp
srv.Put("/api/v4/uploads/chunk",
```

### Line 25325: POST `/api/v4/uploads/cancel`

```cpp
srv.Post("/api/v4/uploads/cancel", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 25369: POST `/api/v4/uploads/finish`

```cpp
srv.Post("/api/v4/uploads/finish", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 25797: POST `/api/v4/files/move`

```cpp
srv.Post("/api/v4/files/move", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 26690: POST `/api/v4/files/mkdir`

```cpp
srv.Post("/api/v4/files/mkdir", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 26913: POST `/api/v4/files/hash`

```cpp
srv.Post("/api/v4/files/hash", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 27088: POST `/api/v4/files/rmdir`

```cpp
srv.Post("/api/v4/files/rmdir", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 27403: POST `/api/v4/files/tree`

```cpp
srv.Post("/api/v4/files/tree", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 27662: POST `/api/v4/files/touch`

```cpp
srv.Post("/api/v4/files/touch", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 27919: POST `/api/v4/files/cat`

```cpp
srv.Post("/api/v4/files/cat", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 28112: POST `/api/v4/files/save_text`

```cpp
srv.Post("/api/v4/files/save_text", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 28574: GET `/api/v4/files/read_text`

```cpp
srv.Get("/api/v4/files/read_text", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 28798: POST `/api/v4/files/write_text`

```cpp
srv.Post("/api/v4/files/write_text", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 29164: POST `/api/v4/files/zip`

```cpp
srv.Post("/api/v4/files/zip", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 29516: POST `/api/v4/files/zip_sel`

```cpp
srv.Post("/api/v4/files/zip_sel", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 30098: POST `/api/v4/files/rmrf`

```cpp
srv.Post("/api/v4/files/rmrf", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 30823: POST `/api/v4/files/search`

```cpp
srv.Post("/api/v4/files/search", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 31494: POST `/api/v4/files/stat`

```cpp
srv.Post("/api/v4/files/stat", files_stat_handler);
```

### Line 31495: GET `/api/v4/files/stat`

```cpp
srv.Get ("/api/v4/files/stat", files_stat_handler);
```

### Line 31500: POST `/api/v4/files/stat_sel`

```cpp
srv.Post("/api/v4/files/stat_sel", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 31821: POST `/api/v4/files/du`

```cpp
srv.Post("/api/v4/files/du", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 32035: GET `/api/v4/files/favorites`

```cpp
srv.Get("/api/v4/files/favorites", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 32077: POST `/api/v4/files/favorites/add`

```cpp
srv.Post("/api/v4/files/favorites/add", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 32144: POST `/api/v4/files/favorites/remove`

```cpp
srv.Post("/api/v4/files/favorites/remove", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 32199: POST `/api/v4/files/delete`

```cpp
srv.Post("/api/v4/files/delete", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 33045: GET `/api/v4/files/list`

```cpp
srv.Get("/api/v4/files/list", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 33328: GET `/api/v4/me/storage`

```cpp
srv.Get("/api/v4/me/storage", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 33531: POST `/api/v4/files/exists`

```cpp
srv.Post("/api/v4/files/exists", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 33689: POST `/api/v4/files/copy`

```cpp
srv.Post("/api/v4/files/copy", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 34336: GET `/api/v4/files/zip`

```cpp
srv.Get("/api/v4/files/zip", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 34780: GET `/api/v4/files/office_preview`

```cpp
srv.Get("/api/v4/files/office_preview", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 35048: GET `/api/v4/files/get`

```cpp
srv.Get("/api/v4/files/get", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 35271: GET `/api/v4/gallery/list`

```cpp
srv.Get("/api/v4/gallery/list", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 35848: GET `/api/v4/music/cover`

```cpp
srv.Get("/api/v4/music/cover", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 36794: GET `/api/v4/gallery/tree_stats`

```cpp
srv.Get("/api/v4/gallery/tree_stats", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 36914: GET `/api/v4/gallery/search`

```cpp
srv.Get("/api/v4/gallery/search", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 37457: GET `/api/v4/reelstack/index`

```cpp
srv.Get("/api/v4/reelstack/index", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 37527: POST `/api/v4/reelstack/scan`

```cpp
srv.Post("/api/v4/reelstack/scan", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 37887: GET `/api/v4/reelstack/thumb`

```cpp
srv.Get("/api/v4/reelstack/thumb", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 38234: GET `/api/v4/reelstack/meta`

```cpp
srv.Get("/api/v4/reelstack/meta", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 38742: GET `/api/v4/reelstack/user_meta`

```cpp
srv.Get("/api/v4/reelstack/user_meta", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 38801: POST `/api/v4/reelstack/meta/set`

```cpp
srv.Post("/api/v4/reelstack/meta/set", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 38931: GET `/api/v4/gallery/thumb`

```cpp
srv.Get("/api/v4/gallery/thumb", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 39503: POST `/api/v4/gallery/meta/set`

```cpp
srv.Post("/api/v4/gallery/meta/set", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 39972: POST `/api/v4/gallery/meta/get`

```cpp
srv.Post("/api/v4/gallery/meta/get", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 40251: GET `/api/v4/photogallery/stats`

```cpp
srv.Get("/api/v4/photogallery/stats", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 40500: PUT `/api/v4/files/put`

```cpp
srv.Put("/api/v4/files/put",
```

### Line 41343: GET `/api/v4/files/archive_manifest`

```cpp
srv.Get("/api/v4/files/archive_manifest", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41414: GET `/api/v4/files/versions/archive_manifest`

```cpp
srv.Get("/api/v4/files/versions/archive_manifest", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41480: GET `/api/v4/files/versions/blob`

```cpp
srv.Get("/api/v4/files/versions/blob", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41569: GET `/api/v4/files/versions/list`

```cpp
srv.Get("/api/v4/files/versions/list", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41660: GET `/api/v4/files/versions/read_text`

```cpp
srv.Get("/api/v4/files/versions/read_text", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41750: GET `/api/v4/files/versions/download`

```cpp
srv.Get("/api/v4/files/versions/download", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41931: POST `/api/v4/files/versions/flag`

```cpp
srv.Post("/api/v4/files/versions/flag", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41935: POST `/api/v4/files/versions/unflag`

```cpp
srv.Post("/api/v4/files/versions/unflag", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41940: GET `/api/v4/files/versions/summary`

```cpp
srv.Get("/api/v4/files/versions/summary", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 41967: POST `/api/v4/files/versions/delete`

```cpp
srv.Post("/api/v4/files/versions/delete", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 42044: POST `/api/v4/files/restore_version`

```cpp
srv.Post("/api/v4/files/restore_version", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 42226: POST `/api/v4/snapshots/create`

```cpp
srv.Post("/api/v4/snapshots/create", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 42569: GET `/api/v4/snapshots/volumes`

```cpp
srv.Get("/api/v4/snapshots/volumes", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 42629: GET `/api/v4/snapshots/list`

```cpp
srv.Get("/api/v4/snapshots/list", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 42797: GET `/api/v4/snapshots/info`

```cpp
srv.Get("/api/v4/snapshots/info", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 42864: POST `/api/v4/snapshots/restore/prepare`

```cpp
srv.Post("/api/v4/snapshots/restore/prepare", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 42996: GET `/api/v4/snapshots/restore/status`

```cpp
srv.Get("/api/v4/snapshots/restore/status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43134: POST `/api/v4/snapshots/restore/confirm`

```cpp
srv.Post("/api/v4/snapshots/restore/confirm", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43409: POST `/api/v4/admin/users/upsert`

```cpp
srv.Post("/api/v4/admin/users/upsert", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43528: POST `/api/v4/admin/users/enable`

```cpp
srv.Post("/api/v4/admin/users/enable", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43588: POST `/api/v4/admin/users/avatar_upload`

```cpp
srv.Post("/api/v4/admin/users/avatar_upload", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43696: GET `/api/v4/admin/users/avatar`

```cpp
srv.Get("/api/v4/admin/users/avatar", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43714: GET `/api/v4/users/avatar`

```cpp
srv.Get("/api/v4/users/avatar", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43791: POST `/api/v4/admin/users/avatar_remove`

```cpp
srv.Post("/api/v4/admin/users/avatar_remove", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43838: POST `/api/v4/user/profile/avatar_upload`

```cpp
srv.Post("/api/v4/user/profile/avatar_upload", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 43999: POST `/api/v4/user/profile/avatar_remove`

```cpp
srv.Post("/api/v4/user/profile/avatar_remove", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 44066: GET `/api/v4/apps/has`

```cpp
srv.Get("/api/v4/apps/has", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 44204: GET `/api/v4/apps/list`

```cpp
srv.Get("/api/v4/apps/list", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 44332: POST `/api/v4/apps/upload_install`

```cpp
srv.Post("/api/v4/apps/upload_install", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 44588: POST `/api/v4/apps/install_bundled`

```cpp
srv.Post("/api/v4/apps/install_bundled", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 44758: POST `/api/v4/apps/launch_policy`

```cpp
srv.Post("/api/v4/apps/launch_policy", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 44868: POST `/api/v4/apps/uninstall`

```cpp
srv.Post("/api/v4/apps/uninstall", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 44956: POST `/api/v4/admin/users/disable`

```cpp
srv.Post("/api/v4/admin/users/disable", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 45021: GET `/api/debug/auth/approvals`

```cpp
srv.Get("/api/debug/auth/approvals", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 45050: POST `/api/v4/admin/users/delete`

```cpp
srv.Post("/api/v4/admin/users/delete", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 45127: POST `/api/v4/shares/pq/enroll`

```cpp
srv.Post("/api/v4/shares/pq/enroll", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 45635: POST `/api/v4/shares/pq/open/init`

```cpp
srv.Post("/api/v4/shares/pq/open/init", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 45812: GET `/api/v4/shares/pq/open/chunk`

```cpp
srv.Get("/api/v4/shares/pq/open/chunk", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 45940: POST `/api/v4/shares/pq/open`

```cpp
srv.Post("/api/v4/shares/pq/open", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 46141: POST `/api/v4/shares/pq/recipient/update`

```cpp
srv.Post("/api/v4/shares/pq/recipient/update", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 46388: POST `/api/v4/shares/create`

```cpp
srv.Post("/api/v4/shares/create", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 46888: POST `/api/v4/shares/revoke`

```cpp
srv.Post("/api/v4/shares/revoke", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 46998: GET `/api/v4/shares/list`

```cpp
srv.Get("/api/v4/shares/list", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 47148: POST `/api/v4/admin/users/migrate_storage`

```cpp
srv.Post("/api/v4/admin/users/migrate_storage", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 47266: GET `/api/v4/admin/users/migrate_storage_status`

```cpp
srv.Get("/api/v4/admin/users/migrate_storage_status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 47299: POST `/api/v4/admin/users/cleanup_old_storage`

```cpp
srv.Post("/api/v4/admin/users/cleanup_old_storage", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 47393: GET `/api/v4/admin/users/cleanup_old_storage_status`

```cpp
srv.Get("/api/v4/admin/users/cleanup_old_storage_status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 47426: POST `/api/v4/admin/storage/tiering/migrate_one`

```cpp
srv.Post("/api/v4/admin/storage/tiering/migrate_one", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 47504: POST `/api/v4/gallery/export_sel_zip`

```cpp
srv.Post("/api/v4/gallery/export_sel_zip", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 48461: GET `/api/v4/admin/storage/tiering/status`

```cpp
srv.Get("/api/v4/admin/storage/tiering/status", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 48663: GET `/api/public/gallery/album/image`

```cpp
srv.Get("/api/public/gallery/album/image", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 48723: GET `/s/([A-Za-z0-9_-]+`

```cpp
srv.Get(R"(/s/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 49445: POST `/api/v4/gallery/meta/embedded_get`

```cpp
srv.Post("/api/v4/gallery/meta/embedded_get", [&](const httplib::Request& req, httplib::Response& res) {
```

### Line 49741: GET `/pq/invite/([A-Za-z0-9_-]+`

```cpp
srv.Get(R"(/pq/invite/([A-Za-z0-9_-]+))", [&](const httplib::Request& req, httplib::Response& res) {
```

