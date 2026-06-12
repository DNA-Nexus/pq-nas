# DNA-Nexus Server API Overview

## Status

Draft.

## Purpose

This document explains the high-level API layout of DNA-Nexus Server.

The generated route inventory is maintained separately in:

```text
docs/technical/api-main-routes.md
```

That generated file lists routes discovered from `server/src/main.cpp`. This overview explains the API groups, trust levels, and intended use.

## API Documentation Model

DNA-Nexus API documentation is split into two layers:

1. **Generated route inventory**
   - Extracted from source code.
   - Useful for finding all registered routes.
   - May contain TODO descriptions or guessed auth classification.

2. **Human-written API overview**
   - Explains API groups and intent.
   - Documents trust boundaries.
   - Describes the expected caller type.
   - Should be kept stable and readable.

## Main Route Groups

### Public/static routes

Examples:

```text
GET /
GET /static/app.js
GET /static/theme.css
GET /static/theme.js
GET /wait-approval
```

Purpose:

- serve the main browser UI
- serve static frontend assets
- serve public landing/status pages used before full user access

Auth model:

```text
Public/static
```

These routes may be reachable without an authenticated user session, but they must not expose private user data.

### Login/session routes

Examples:

```text
GET  /api/auth/config
POST /api/v5/session
POST /api/v5/status
POST /api/v5/consume
POST /api/auth/password/login
POST /api/auth/opaque/login/start
POST /api/auth/opaque/login/finish
```

Purpose:

- support browser login through QR, classic password, or OPAQUE modes
- support DNA identity / device-mediated verification
- expose safe public authentication mode information
- keep all successful browser login methods mapped to the same internal fingerprint/session model

Auth model:

```text
Public entry / session flow
```

The browser may start the flow, but access is granted only after server-side identity/session verification.

Important rule:

```text
The browser is not the root of trust.
```

### User session API

Examples:

```text
GET  /api/v4/me
GET  /api/v4/me/storage
GET  /api/v4/apps
GET  /api/v4/files/list
POST /api/v4/files/mkdir
POST /api/v4/files/delete
PUT  /api/v4/files/put
```

Purpose:

- normal logged-in user operations
- file manager behavior
- user profile and storage information
- app listing and app launching
- media/gallery access
- uploads and downloads

Auth model:

```text
User session
```

These routes require a valid user session and must enforce per-user authorization.

### Admin API

Examples:

```text
GET  /admin
GET  /api/v4/admin/users
POST /api/v4/admin/users/upsert
POST /api/v4/admin/users/disable
POST /api/v4/admin/settings
GET  /api/v4/admin/stats/summary
```

Purpose:

- admin UI
- user management
- settings management
- audit management
- storage/admin operations
- stats and system visibility

Auth model:

```text
Admin session
```

These routes must require an authenticated admin user.

Important rule:

```text
A normal user session must not be enough for admin routes.
```

### File API

Examples:

```text
GET  /api/v4/files/list
GET  /api/v4/files/get
POST /api/v4/files/mkdir
POST /api/v4/files/rmdir
POST /api/v4/files/delete
POST /api/v4/files/move
POST /api/v4/files/copy
POST /api/v4/files/search
PUT  /api/v4/files/put
```

Purpose:

- browse files
- upload/download files
- create/delete folders
- move/copy files
- search files
- read/write text files
- calculate hashes
- create zip exports

Auth model:

```text
User session
```

Security requirements:

- normalize all browser-supplied paths
- verify path containment
- enforce per-user access
- enforce workspace role where applicable
- enforce quota before writes
- avoid exposing absolute local filesystem paths

Important rule:

```text
A browser-supplied path is never automatically trusted.
```

### Upload API

Examples:

```text
POST /api/v4/uploads/start
PUT  /api/v4/uploads/chunk
POST /api/v4/uploads/finish
POST /api/v4/uploads/cancel
```

Purpose:

- chunked upload flow
- large file upload support
- staged upload completion
- upload cancellation

Auth model:

```text
User session
```

Security requirements:

- validate upload session ownership
- enforce quota before finalizing
- write through safe temporary/staging paths
- prevent path traversal
- avoid partial uploads becoming visible as complete files

### Shares API

Examples:

```text
POST /api/v4/shares/create
GET  /api/v4/shares/list
POST /api/v4/shares/revoke
GET  /s/<token>
```

Purpose:

- create public share links
- list active shares
- revoke shares
- open public shared content by token

Auth model:

```text
User session for management.
Public token/link for opening.
```

Security requirements:

- share tokens must be unguessable
- public share route must expose only intended file/content
- revoked/expired shares must not remain usable
- public routes must not expose owner internals or local filesystem paths

### Gallery and media API

Examples:

```text
GET  /api/v4/gallery/list
GET  /api/v4/gallery/search
GET  /api/v4/gallery/thumb
POST /api/v4/gallery/meta/get
POST /api/v4/gallery/meta/set
GET  /api/v4/music/cover
```

Purpose:

- photo gallery views
- thumbnails
- metadata
- media search
- cover image serving

Auth model:

```text
User session
```

Security requirements:

- media access must follow the same authorization rules as files
- thumbnail/cache generation must not bypass quota or path checks
- metadata writes must be scoped to the owning user/content

### Storage, RAID, and pool management API

Examples:

```text
GET  /api/v4/storage/status
GET  /api/v4/storage/pools
GET  /api/v4/storage/disks
GET  /api/v4/raid/status
POST /api/v4/raid/plan/create-pool
POST /api/v4/raid/execute/create-pool
```

Purpose:

- inspect storage state
- manage storage pools
- plan RAID/storage operations
- execute selected storage operations
- monitor jobs and health

Auth model:

```text
User session or admin session depending on route.
```

Open documentation task:

Some storage routes currently appear as `User session` in the generated route inventory. These should be reviewed manually because destructive storage operations may need admin-only enforcement.

### App API

Examples:

```text
GET  /api/v4/apps
GET  /api/v4/apps/list
GET  /api/v4/apps/has
POST /api/v4/apps/install_bundled
POST /api/v4/apps/upload_install
POST /api/v4/apps/uninstall
POST /api/v4/apps/launch_policy
```

Purpose:

- list installed apps
- check app availability
- install bundled apps
- upload/install apps
- uninstall apps
- control launch policy

Auth model:

```text
User session or admin session depending on route.
```

Open documentation task:

App install/uninstall policy should be reviewed and documented separately. Some app-management operations may need admin-only rules.

### Snapshot API

Examples:

```text
POST /api/v4/snapshots/create
GET  /api/v4/snapshots/list
GET  /api/v4/snapshots/info
POST /api/v4/snapshots/restore/prepare
POST /api/v4/snapshots/restore/confirm
GET  /api/v4/snapshots/restore/status
```

Purpose:

- create snapshots
- list snapshots
- inspect snapshot details
- prepare restore operation
- confirm restore operation
- monitor restore status

Auth model:

```text
User session or admin session depending on scope.
```

Security requirements:

- restore operations must be carefully authorized
- snapshot paths must not expose raw local filesystem internals
- destructive restore behavior should require explicit confirmation

## Generated Inventory Rules

The generated inventory should be treated as a draft.

It is good for:

- finding routes
- spotting duplicate route registrations
- spotting route groups still inside `main.cpp`
- finding undocumented public/token routes
- guiding refactoring work

It is not enough for:

- final security review
- customer-facing API documentation
- exact request/response schemas
- proving authorization behavior

## Documentation Priorities

Recommended next API documentation work:

1. Review all `Unknown` auth routes.
2. Review all storage/RAID/app-management routes for admin-vs-user access.
3. Add request/response examples for login/session routes.
4. Add request/response examples for file operations.
5. Add request/response examples for share links.
6. Document Drop Zone separately because it has both owner-authenticated and public-token surfaces.
7. Document workspace and external invite routes from their dedicated route files.
8. Later, generate an OpenAPI file only after the Markdown docs are stable.

## Things We Must Not Break

- Public/static routes must not leak private data.
- Public token routes must expose only the intended shared/upload surface.
- User session routes must enforce user ownership and workspace role rules.
- Admin routes must require admin authorization.
- File paths must be normalized and contained.
- Uploads must enforce quota and safe staging.
- Generated docs must not be treated as security truth until manually reviewed.
