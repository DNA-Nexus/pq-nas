# DNA-Nexus Server File Access Security

## Status

Draft.

## Purpose

This document describes the security model for file access in DNA-Nexus Server.

It is paired with:

```text
docs/technical/file-api.md
```

The File API is one of the most security-critical parts of DNA-Nexus Server because it handles user files, uploads, downloads, archives, previews, versions, public shares, and workspace content.

## Core Rule

```text
A browser-supplied path is never automatically trusted.
```

Every file operation must be checked on the server.

The browser may ask for a path, but the server decides whether that path is valid, contained, authorized, and safe to use.

## Main Security Goals

The file access layer must prevent:

- reading another user's files
- writing into another user's storage
- escaping the intended storage root
- path traversal using `../`
- absolute path injection
- symlink-based escape if symlinks become user-controllable
- quota bypass
- public share overexposure
- workspace role bypass
- leaking local absolute filesystem paths
- archive/zip operations including unauthorized files
- preview/thumbnail generation bypassing normal authorization
- version history exposing files that should no longer be accessible

## Trust Boundaries

### Browser to server

The browser is untrusted.

The browser can send:

- paths
- filenames
- MIME types
- destination folders
- overwrite flags
- selected file lists
- archive requests
- text content
- metadata edits

None of these should be trusted without validation.

### Server to filesystem

The server is responsible for translating user-facing paths into safe filesystem paths.

The filesystem must not be exposed directly to the browser.

Important rule:

```text
User path -> normalize -> authorize -> resolve -> operate
```

Never:

```text
User path -> direct filesystem operation
```

### User storage root

Each user must have a defined allowed storage root or resolved storage context.

File operations must stay inside that root unless the user is explicitly operating inside an authorized workspace or controlled share context.

### Workspace storage

Workspace access must be role-based.

Example roles:

- viewer
- editor
- owner

Expected permissions:

| Role | Read | Upload/write | Delete | Manage members |
|---|---:|---:|---:|---:|
| viewer | yes | no | no | no |
| editor | yes | yes | maybe, depending on policy | no |
| owner | yes | yes | yes | yes |

The exact policy should be documented in the workspace documentation, but File API implementation must not assume all workspace members can write or delete.

## Path Normalization Requirements

Every path from the browser should be normalized before use.

The server should reject or safely handle:

- empty unsafe paths
- absolute paths such as `/etc/passwd`
- Windows-style absolute paths such as `C:\Users\...`
- parent traversal such as `../`
- repeated separators
- encoded traversal attempts
- hidden internal metadata paths if those are reserved
- paths that resolve outside the allowed root

Examples of unsafe input:

```text
../../etc/passwd
/home/timo/.ssh/id_rsa
/srv/pqnas/users/other-user/private.txt
folder/../../../other-user/file.txt
C:\Users\Alice\secret.txt
```

Expected behavior:

```text
Reject request.
Return safe error.
Do not leak local filesystem details.
Write audit event where useful.
```

## Path Containment

After normalization and resolution, the final target must still be inside the allowed root.

Conceptual rule:

```text
resolved_target must be inside allowed_root
```

This check must be applied to:

- reads
- writes
- uploads
- mkdir
- delete
- recursive delete
- copy
- move
- zip/archive generation
- thumbnail/preview generation
- version restore
- metadata operations

## Symlink Considerations

Current implementations may use lexical containment checks in some areas.

Lexical checks can be acceptable only when user-controlled symlinks are not allowed or not followed in a dangerous way.

Future hardening should consider symlink-safe patterns for write-sensitive operations, such as:

- open relative to a trusted directory handle
- avoid following symlinks where not intended
- verify final resolved path
- prevent user-controlled symlink escape from storage roots

Important future hardening note:

```text
If users can create symlinks inside storage roots, lexical containment is not enough.
```

## Read Operations

Read operations include:

```text
GET  /api/v4/files/get
GET  /api/v4/files/read_text
POST /api/v4/files/cat
GET  /api/v4/files/office_preview
GET  /api/v4/files/versions/download
GET  /api/v4/files/versions/read_text
```

Security requirements:

- require valid user session unless route is intentionally public-token based
- authorize path before reading
- prevent reading outside the allowed root
- avoid returning local absolute paths
- handle large files safely
- avoid treating binary files as safe text
- ensure previews follow the same authorization as normal reads

Failure mode should be safe:

```text
Return 403 or 404.
Do not reveal whether another user's file exists.
Do not reveal local filesystem paths.
```

## Write Operations

Write operations include:

```text
PUT  /api/v4/files/put
POST /api/v4/files/save_text
POST /api/v4/files/write_text
POST /api/v4/files/touch
POST /api/v4/files/mkdir
POST /api/v4/files/move
POST /api/v4/files/copy
POST /api/v4/files/restore_version
```

Security requirements:

- require valid user session
- authorize destination
- enforce quota before finalizing writes
- use safe temporary files where practical
- avoid exposing partial/corrupt files as complete files
- protect internal metadata directories
- validate overwrite behavior
- create audit/activity events where useful

Important rule:

```text
Quota must be checked before accepting or finalizing writes.
```

## Delete Operations

Delete operations include:

```text
POST /api/v4/files/delete
POST /api/v4/files/rmdir
POST /api/v4/files/rmrf
POST /api/v4/files/versions/delete
```

Security requirements:

- require valid user session
- authorize target
- prevent deleting outside allowed root
- prevent deleting internal server metadata
- treat recursive delete as high risk
- log destructive operations where useful
- ensure workspace role allows delete

Important rule:

```text
Recursive delete must never operate on an untrusted raw path.
```

## Copy and Move Operations

Copy and move operations have two paths:

```text
source
destination
```

Both paths must be checked.

Security requirements:

- source must be readable by caller
- destination must be writable by caller
- destination must stay inside allowed root/workspace
- copy must enforce quota
- move must not be allowed to escape storage boundaries
- cross-user moves must be forbidden unless explicitly designed
- move/copy must not expose internal paths in errors

Risky case:

```text
source is authorized, destination escapes allowed root
```

Expected behavior:

```text
Reject request.
```

Risky case:

```text
source is not authorized, destination is authorized
```

Expected behavior:

```text
Reject request.
```

## Upload Security

Upload routes include:

```text
POST /api/v4/uploads/start
PUT  /api/v4/uploads/chunk
POST /api/v4/uploads/finish
POST /api/v4/uploads/cancel
PUT  /api/v4/files/put
```

Security requirements:

- upload session must belong to the authenticated user
- destination path must be validated at start and finish
- chunks must be written only to safe staging paths
- final assembly must enforce quota
- cancelled/expired uploads should not leave visible partial files
- chunk IDs and upload IDs must not allow path injection
- content type from browser is advisory only

Important rule:

```text
Temporary upload storage must not become a bypass around final authorization.
```

## Archive and Zip Security

Archive routes include:

```text
GET  /api/v4/files/zip
POST /api/v4/files/zip
POST /api/v4/files/zip_sel
GET  /api/v4/files/archive_manifest
GET  /api/v4/files/versions/archive_manifest
```

Security requirements:

- include only authorized files
- normalize every selected path
- avoid path traversal inside archive names
- avoid absolute paths inside archive entries
- apply size/count limits where practical
- avoid leaking hidden/internal metadata files
- ensure archive manifest matches authorized contents only

Important rule:

```text
Archive generation must not be a way to read files that normal download cannot read.
```

## Preview and Thumbnail Security

Preview routes include:

```text
GET /api/v4/files/office_preview
GET /api/v4/gallery/thumb
GET /api/v4/music/cover
```

Security requirements:

- preview access must follow normal file authorization
- thumbnail/cache files must not be directly exposed without authorization
- preview generation must not read files outside the allowed root
- generated previews should not leak local paths or internal command output
- unsupported formats should fail safely

Important rule:

```text
A preview route is still a file read route.
```

## File Version Security

Version routes include:

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
```

Security requirements:

- version access must follow current file authorization
- old versions must not be visible to unauthorized users
- public links should not automatically expose private version history
- restoring a version is a write operation
- deleting a version is a destructive operation
- version metadata must not leak local paths

Open policy question:

```text
If a file is shared publicly, should old versions be public too?
```

Safe default:

```text
No. Public links should expose only the intended current object unless explicitly designed otherwise.
```

## Public Share Security

Public share routes include:

```text
GET /s/<token>
```

and share management routes include:

```text
POST /api/v4/shares/create
GET  /api/v4/shares/list
POST /api/v4/shares/revoke
```

Security requirements:

- management requires user session
- public open requires valid token
- tokens must be high entropy
- revoked shares must stop working
- expired shares must stop working
- public response must not expose local paths
- public route must expose only the selected shared file/content
- public route must not expose parent folders unless explicitly designed

Important rule:

```text
A public share token is not a user session.
```

## Drop Zone Security Relationship

Drop Zone is related to file access but has its own security model.

Drop Zone is one-way:

```text
External uploader can upload.
External uploader cannot browse.
External uploader cannot download destination contents.
```

Drop Zone security is documented separately because it has both:

- owner-authenticated management API
- public token upload API

Important shared requirement:

```text
Drop Zone uploads must enforce quota and destination containment.
```

## Error Handling

Errors should be safe.

Good error style:

```text
403 forbidden
404 not found
400 invalid path
413 upload too large
507 insufficient storage/quota
```

Avoid errors that reveal:

- local absolute paths
- other users' usernames or fingerprints
- internal metadata locations
- stack traces
- shell command output
- exact existence of private files belonging to another user

## Audit and Activity Events

Useful events to log:

- failed path traversal attempt
- denied access
- upload rejected due to quota
- recursive delete
- public share created
- public share revoked
- version restored
- version deleted
- workspace write/delete
- Drop Zone upload completed
- Drop Zone upload rejected

Not every normal read needs an audit event, but security-sensitive and destructive actions should be visible.

## Checklist for New File Routes

Before adding a new file-related route, answer:

1. Is the caller public, user-session, admin-session, or token-based?
2. What path or object does it operate on?
3. Is every browser-supplied path normalized?
4. Is final path containment checked?
5. Is ownership/workspace role checked?
6. Does it read, write, delete, copy, move, or generate derived content?
7. Does it need quota enforcement?
8. Does it expose local paths?
9. Does it need audit/activity logging?
10. What happens on symlink or traversal input?
11. What happens if the target is huge?
12. What happens if the file disappears during operation?

## Things We Must Not Break

- A user must not read another user's private files.
- A user must not write into another user's storage.
- Public share routes must not become folder browsing routes accidentally.
- Drop Zone must remain one-way.
- Workspace roles must be enforced by the backend.
- Quota checks must happen before writes are finalized.
- Zip/archive generation must not include unauthorized files.
- Preview/thumbnail routes must not bypass authorization.
- Version history must not expose private content unexpectedly.
- Error messages must not leak local filesystem details.
