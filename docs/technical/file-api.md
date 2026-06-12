# DNA-Nexus Server File API

## Status

Draft.

## Purpose

This document describes the main File API routes used by DNA-Nexus Server.

The generated route inventory is in:

```text
docs/technical/api-main-routes.md
```

This document is the human-written explanation for the `/api/v4/files/*` route group.

## Trust Model

The File API is a user-session API.

A caller must have a valid DNA-Nexus user session. The browser is treated as a UI surface, not as the root of trust.

Important rule:

```text
A browser-supplied path is never automatically trusted.
```

Every file operation must be checked server-side.

## Security Requirements

All File API routes must enforce:

- authenticated user session
- path normalization
- path containment
- user ownership or workspace role authorization
- quota enforcement before writes
- safe handling of destination paths
- no exposure of local absolute filesystem paths
- no trust in browser-provided paths, MIME types, or filenames without validation

## Main Route Groups

### Listing and metadata

Routes:

```text
GET  /api/v4/files/list
GET  /api/v4/files/stat
POST /api/v4/files/stat
POST /api/v4/files/stat_sel
POST /api/v4/files/du
POST /api/v4/files/exists
POST /api/v4/files/tree
```

Purpose:

- list folder contents
- get file/folder metadata
- calculate selected item metadata
- calculate disk usage
- check existence
- return folder tree data

Expected caller:

```text
File Manager frontend
```

Auth:

```text
User session
```

Important notes:

- listing must only show files the user is allowed to see
- metadata must not leak local absolute paths
- recursive/tree operations must avoid excessive cost or abuse

---

### Reading and downloading

Routes:

```text
GET  /api/v4/files/get
GET  /api/v4/files/read_text
POST /api/v4/files/cat
GET  /api/v4/files/office_preview
```

Purpose:

- download or stream file content
- read text files
- preview supported office/document formats
- return content for viewer/editor surfaces

Expected caller:

```text
File Manager frontend
Preview/viewer UI
Text editor UI
```

Auth:

```text
User session
```

Important notes:

- file read must enforce ownership/workspace permissions
- previews must not bypass file authorization
- large files should be handled carefully
- text reads should have size limits or safe behavior for large/binary files

---

### Writing and editing

Routes:

```text
PUT  /api/v4/files/put
POST /api/v4/files/save_text
POST /api/v4/files/write_text
POST /api/v4/files/touch
```

Purpose:

- upload/replace a file
- save text content
- create or update text files
- touch/create empty files

Expected caller:

```text
File Manager frontend
Text editor UI
Upload UI
```

Auth:

```text
User session
```

Important notes:

- quota must be checked before accepting writes
- destination path must be normalized and contained
- writes should avoid partial/corrupt visible files
- versioning behavior should be documented where applicable
- overwrites should be intentional and controlled

---

### Folder operations

Routes:

```text
POST /api/v4/files/mkdir
POST /api/v4/files/rmdir
POST /api/v4/files/rmrf
```

Purpose:

- create folders
- remove empty folders
- remove folder trees

Expected caller:

```text
File Manager frontend
```

Auth:

```text
User session
```

Important notes:

- recursive delete is destructive and must be carefully authorized
- paths must be normalized
- root/user storage boundaries must not be crossed
- errors should be clear but not leak internals

---

### Move, copy, and delete

Routes:

```text
POST /api/v4/files/move
POST /api/v4/files/copy
POST /api/v4/files/delete
```

Purpose:

- move files/folders
- copy files/folders
- delete selected files/folders

Expected caller:

```text
File Manager frontend
```

Auth:

```text
User session
```

Important notes:

- source and destination must both be authorized
- copy operations must enforce quota
- move operations must prevent escaping allowed roots
- delete behavior should be consistent with trash/versioning policy if enabled

---

### Search and favorites

Routes:

```text
POST /api/v4/files/search
GET  /api/v4/files/favorites
POST /api/v4/files/favorites/add
POST /api/v4/files/favorites/remove
```

Purpose:

- search user-visible files
- list favorite files/folders
- add/remove favorites

Expected caller:

```text
File Manager frontend
```

Auth:

```text
User session
```

Important notes:

- search results must follow the same authorization rules as normal listing
- favorites must be per-user
- favorites must not expose stale paths from inaccessible locations

---

### Hashing and archives

Routes:

```text
POST /api/v4/files/hash
GET  /api/v4/files/zip
POST /api/v4/files/zip
POST /api/v4/files/zip_sel
GET  /api/v4/files/archive_manifest
```

Purpose:

- calculate file hash
- create/download zip archives
- export selected files
- produce archive manifests

Expected caller:

```text
File Manager frontend
Export/download UI
```

Auth:

```text
User session
```

Important notes:

- archive generation must only include authorized files
- archive paths must not include unsafe local path information
- large zip operations should be bounded or backgrounded
- hash operation should avoid excessive cost on very large files where needed

---

### File versions

Routes:

```text
GET  /api/v4/files/versions/list
GET  /api/v4/files/versions/summary
GET  /api/v4/files/versions/blob
GET  /api/v4/files/versions/download
GET  /api/v4/files/versions/read_text
POST /api/v4/files/versions/flag
POST /api/v4/files/versions/unflag
POST /api/v4/files/versions/delete
POST /api/v4/files/restore_version
GET  /api/v4/files/versions/archive_manifest
```

Purpose:

- list file versions
- inspect version summaries
- download/read old versions
- flag/unflag versions
- delete versions
- restore a previous version

Expected caller:

```text
File Manager frontend
Version history UI
Text editor / preview UI
```

Auth:

```text
User session
```

Important notes:

- version access must follow original file authorization
- restoring a version is a write operation and must enforce quota/storage policy
- deleting versions should be auditable
- public links must not automatically gain access to private version history unless explicitly designed

## Open Questions

- Should destructive File API operations require stronger confirmation for some contexts?
- Which routes should write audit events?
- Should zip/archive generation become a background job for large selections?
- Should text editor routes have explicit maximum file sizes?
- Should file version restore always create a new version before replacing current content?
- Should favorite paths be automatically cleaned when files are deleted or moved?

## Documentation TODO

Add request/response examples for:

```text
GET  /api/v4/files/list
GET  /api/v4/files/get
PUT  /api/v4/files/put
POST /api/v4/files/mkdir
POST /api/v4/files/delete
POST /api/v4/files/move
POST /api/v4/files/copy
POST /api/v4/files/search
```

Add implementation notes from `server/src/main.cpp` after route handlers are reviewed manually.
