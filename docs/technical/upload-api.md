# DNA-Nexus Server Upload API

## Status

Draft.

## Purpose

This document describes the chunked upload API used by DNA-Nexus Server.

The Upload API is used when the frontend needs to upload files in a controlled staged flow instead of sending a full file directly through a single request.

This document is paired with:

```text
docs/technical/file-api.md
docs/security/file-access-security.md
```

## Main Routes

```text
POST /api/v4/uploads/start
PUT  /api/v4/uploads/chunk
POST /api/v4/uploads/finish
POST /api/v4/uploads/cancel
```

## Trust Model

The Upload API is a user-session API.

The caller must have a valid DNA-Nexus user session.

Important rule:

```text
Temporary upload storage must not become a bypass around final authorization.
```

The upload flow may stage data temporarily, but the server must still verify destination, quota, ownership, and path safety before the upload becomes visible as a normal file.

## Upload Flow

Typical flow:

```text
1. Client calls /api/v4/uploads/start
2. Server creates an upload session
3. Client sends one or more chunks to /api/v4/uploads/chunk
4. Client calls /api/v4/uploads/finish
5. Server validates and assembles the final file
6. Server moves the completed file into the authorized destination
```

Cancellation flow:

```text
1. Client calls /api/v4/uploads/cancel
2. Server removes temporary upload state
3. Partial chunks are discarded
4. No final file appears in destination
```

## POST /api/v4/uploads/start

Purpose:

```text
Create a new upload session.
```

Expected caller:

```text
File Manager frontend
Upload UI
```

Auth:

```text
User session
```

Typical request data:

```text
destination path
filename
file size
optional overwrite/duplicate behavior
optional metadata
```

Security requirements:

- require valid user session
- normalize destination path
- verify destination containment
- verify destination write permission
- reject unsafe filenames
- initialize upload state under a safe server-controlled temporary path
- do not trust client-provided upload IDs
- record owner/session identity for later chunk and finish checks

Important rule:

```text
Upload session ownership must be server-side.
```

The browser must not be able to create or select arbitrary upload session storage paths.

## PUT /api/v4/uploads/chunk

Purpose:

```text
Write one chunk into an existing upload session.
```

Expected caller:

```text
File Manager frontend
Upload UI
```

Auth:

```text
User session
```

Typical request data:

```text
upload id
chunk index or offset
chunk bytes
```

Security requirements:

- require valid user session
- verify upload session belongs to caller
- verify upload session is still active
- verify chunk index/offset is valid
- write only to server-controlled temporary storage
- prevent chunk ID/path injection
- enforce per-chunk or total upload limits where practical
- avoid exposing partial upload content as a completed file

Important rule:

```text
Chunk upload must not be able to write outside the upload staging area.
```

## POST /api/v4/uploads/finish

Purpose:

```text
Finalize an upload session and make the completed file visible in the destination.
```

Expected caller:

```text
File Manager frontend
Upload UI
```

Auth:

```text
User session
```

Typical request data:

```text
upload id
finalization confirmation
optional expected size/hash
```

Security requirements:

- require valid user session
- verify upload session belongs to caller
- verify all required chunks exist
- verify expected final size where possible
- re-check destination path
- re-check destination write permission
- enforce quota before finalizing
- assemble into a safe temporary final file
- move/rename atomically where practical
- apply duplicate/overwrite/version policy
- clean temporary upload state after success
- write activity/audit event where useful

Important rule:

```text
Finish is the real authorization boundary.
```

Even if `/start` and `/chunk` were accepted, `/finish` must still re-check the security-sensitive conditions.

## POST /api/v4/uploads/cancel

Purpose:

```text
Cancel an active upload session.
```

Expected caller:

```text
File Manager frontend
Upload UI
```

Auth:

```text
User session
```

Typical request data:

```text
upload id
```

Security requirements:

- require valid user session
- verify upload session belongs to caller
- remove temporary chunks/state
- do not delete anything outside upload staging
- return safe result if already cleaned up or missing

Important rule:

```text
Cancel must only clean server-owned temporary upload state.
```

## Filename Safety

The client-provided filename is untrusted.

Unsafe examples:

```text
../../secret.txt
/srv/pqnas/users/admin/private.txt
C:\Users\Alice\secret.txt
invoice.pdf<script>alert(1)</script>
```

Security requirements:

- reject or sanitize path separators
- reject traversal attempts
- strip/control unsafe characters
- avoid raw filename insertion into HTML
- avoid treating file extension as trusted content type
- avoid trusting browser-provided MIME type for security decisions

## Destination Safety

The upload destination is security-sensitive.

The server must verify:

- destination belongs to authenticated user or authorized workspace
- destination path is normalized
- destination is contained inside allowed root
- destination does not point into server metadata paths
- destination does not point into another user's private storage
- destination role allows write/upload

Important rule:

```text
A staged upload is not allowed to choose a destination later that it could not have used at start.
```

Both start and finish should validate destination.

## Quota Enforcement

Quota must be enforced before finalization.

Useful checks:

- user's remaining quota
- workspace quota if applicable
- filesystem free space
- per-upload max size
- temporary upload storage limit
- global admin limit if configured

Important rule:

```text
Chunk staging must not allow unlimited quota bypass.
```

Possible policy:

```text
Reserve quota at start.
Or enforce strict limit during chunks.
Always verify quota again at finish.
```

## Temporary Storage

Temporary upload files should live in a server-controlled staging area.

Requirements:

- path generated by server
- not derived directly from browser filename
- not web-browsable as normal user files
- cleaned after success/cancel/failure/expiry
- scoped to user/session/upload ID
- protected from path traversal

Important rule:

```text
Temporary upload state is internal server state.
```

## Duplicate and Overwrite Policy

Upload finalization may need to handle existing files.

Possible policies:

```text
version
keep_both
reject
replace
```

Security expectations:

- policy must be explicit
- user must have write permission
- overwrite must not affect files outside destination
- versioning should preserve old content where intended
- policy should be consistent with File API and Drop Zone behavior

Safe default:

```text
version
```

## Error Handling

Good errors:

```text
400 invalid upload request
401 unauthenticated
403 forbidden
404 upload not found
409 upload conflict
413 upload too large
507 insufficient storage/quota
```

Avoid leaking:

- temporary filesystem paths
- absolute destination paths
- stack traces
- internal upload directory layout
- other users' upload IDs
- exact existence of private destination files

## Audit and Activity Events

Useful events:

- upload started
- chunk rejected
- upload finished
- upload cancelled
- upload expired/cleaned
- upload rejected by quota
- upload rejected by invalid path
- upload rejected by authorization
- upload replaced/versioned an existing file

Not every chunk needs a full audit event, but rejected chunks and finalization failures can be useful for security troubleshooting.

## Cleanup Requirements

Upload cleanup should handle:

- cancelled uploads
- expired uploads
- failed uploads
- server restart recovery
- orphaned chunks
- incomplete sessions
- old temporary files

Important rule:

```text
Incomplete uploads must not accumulate forever.
```

## Checklist for Upload API Changes

Before changing upload routes, answer:

1. Does the route require user session?
2. Is upload session ownership checked?
3. Are upload IDs server-generated?
4. Can chunk IDs or offsets affect filesystem paths?
5. Is temporary storage server-controlled?
6. Is destination normalized?
7. Is destination containment checked?
8. Is write permission checked?
9. Is quota enforced before finalization?
10. What happens if upload is cancelled?
11. What happens if upload expires?
12. What happens if chunks are missing?
13. What happens if the final file already exists?
14. Are temporary files cleaned?
15. Are safe errors returned?

## Things We Must Not Break

- Upload chunks must not write outside staging.
- Upload IDs must not allow access to another user's upload.
- Temporary staging must not become public file storage.
- Finish must re-check destination and quota.
- Cancel must not delete arbitrary files.
- Filename input must not become a raw path.
- Upload errors must not leak local filesystem paths.
- Incomplete uploads must be cleaned eventually.
