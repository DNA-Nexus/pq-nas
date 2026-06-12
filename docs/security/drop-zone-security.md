# DNA-Nexus Server Drop Zone Security

## Status

Draft.

## Purpose

This document describes the security model for Drop Zone.

Drop Zone allows a DNA-Nexus user to create a public upload link where an external person can upload files into a selected destination without having a full DNA-Nexus account.

Drop Zone is security-sensitive because it intentionally exposes an upload surface to unauthenticated external users.

This document is paired with:

```text
docs/product/drop-zone-prd.md
docs/security/file-access-security.md
```

## Core Security Rule

Drop Zone is one-way.

```text
External uploader can upload.
External uploader cannot browse.
External uploader cannot download destination contents.
External uploader cannot see private owner information.
```

This rule must not be weakened accidentally.

## Surfaces

Drop Zone has two different surfaces.

### Owner/authenticated API

The owner/authenticated API is used by a logged-in DNA-Nexus user.

Typical route group:

```text
/api/v4/dropzones/*
```

Purpose:

- create Drop Zone links
- list existing Drop Zone links
- disable links
- inspect uploads
- configure branding
- configure expiry/password/limits

Auth model:

```text
User session
```

The owner controls the Drop Zone destination and settings.

### Public token API

The public token API is used by an external uploader.

Typical route groups:

```text
/api/public/dropzones/<token>/*
/dz/<token>
```

Purpose:

- open public upload page
- submit optional password
- upload file or chunks
- receive success/failure status

Auth model:

```text
Public token/link
```

The token is the access capability. It must be high entropy and unguessable.

## Token Security

Drop Zone tokens must be treated as bearer capabilities.

Security requirements:

- tokens must be high entropy
- tokens must not be guessable
- raw token should not be stored if avoidable
- token lookup should use a hash/token_hash model where practical
- token_hash must not be exposed publicly
- expired/disabled tokens must stop working
- token errors must not reveal sensitive internal state

Good behavior:

```text
Valid token -> allow intended upload behavior.
Expired token -> reject.
Disabled token -> reject.
Unknown token -> reject.
```

Avoid:

```text
Unknown token but reveal owner exists.
Expired token but reveal destination path.
Disabled token but reveal internal metadata.
```

## Public Response Restrictions

Public Drop Zone responses must not expose:

- owner fingerprint
- token hash
- password hash
- local absolute filesystem paths
- destination path
- internal metadata paths
- server stack traces
- private owner settings
- full user profile
- list of already uploaded files unless explicitly designed

Public response may expose:

- safe page title
- safe description
- safe branding
- upload limit message
- expiry/closed status
- upload success/failure result

## Destination Security

The owner chooses a destination.

The external uploader must not be able to choose an arbitrary filesystem destination.

Security requirements:

- destination is selected by owner through authenticated API
- destination is stored server-side
- public upload requests refer to the token, not raw server paths
- destination must be normalized
- destination must be contained inside owner-allowed storage
- destination must not cross into another user's storage
- internal metadata directories must be protected

Important rule:

```text
Public uploader input must never become a trusted destination path.
```

## Quota Security

Drop Zone uploads must enforce quota.

Required checks:

- owner storage quota
- per-file upload limit
- optional per-link total limit
- server/admin global limit where applicable
- temporary upload storage limits
- final destination capacity

Important rule:

```text
Drop Zone must not become a quota bypass.
```

Quota should be checked before finalizing uploads.

For chunked uploads, temporary chunks must not allow unlimited storage growth.

## Password Security

Drop Zone may support optional password protection.

Security requirements:

- password must not be stored in plaintext
- password hash must not be exposed
- failed password attempts should be rate-limited where practical
- error messages should be generic
- password state must be tied to the token/link
- password bypass via upload endpoints must not be possible

Good error:

```text
Invalid password or link.
```

Bad error:

```text
Password wrong for Drop Zone owned by alice at /srv/pqnas/users/alice/client_uploads.
```

## Expiry and Disabled State

Drop Zone links may expire or be disabled.

Security requirements:

- expiry must be checked server-side
- disabled links must reject upload attempts
- frontend-only expiry is not enough
- old upload sessions should not be finishable after the link is expired/disabled unless explicitly designed
- public page should show a safe closed/expired message

Important rule:

```text
Expiry is an authorization check, not a UI hint.
```

## Upload Staging

Uploads should be staged safely.

Security requirements:

- write chunks/body to safe temporary location
- temporary paths must be server-generated
- upload IDs must not allow path injection
- chunk IDs must not allow path injection
- final assembly must re-check token state, destination, limits, and quota
- partially uploaded files must not appear as completed destination files
- failed/cancelled uploads should be cleaned up

Important rule:

```text
Temporary upload storage must not bypass final authorization.
```

## Filename Safety

External uploaders provide filenames.

Filenames must be treated as untrusted input.

Security requirements:

- reject or normalize path separators
- prevent `../` traversal through filenames
- avoid absolute paths
- sanitize control characters
- handle duplicate filenames according to policy
- avoid exposing unsafe names in HTML without escaping
- do not trust browser-provided MIME type

Unsafe examples:

```text
../../secret.txt
/srv/pqnas/users/admin/private.txt
C:\Users\Alice\secret.txt
invoice.pdf<script>alert(1)</script>
```

Expected behavior:

```text
Normalize or reject safely.
Never use filename as raw path.
```

## Duplicate File Policy

Drop Zone should have a clear duplicate handling policy.

Possible policies:

```text
version
keep_both
reject
```

Security expectations:

- duplicate policy must be owner-controlled
- public uploader must not use duplicate policy to overwrite arbitrary files
- overwrite/version behavior must stay inside destination
- audit/activity record should show what happened

Safe default:

```text
version
```

This preserves previous content instead of silently destroying it.

## Branding Security

Drop Zone supports custom branding.

Branding is useful for businesses, but it must not become arbitrary code execution.

Allowed branding should be structured data, such as:

- title
- description
- accent color
- logo image/data URL if validated
- footer text
- safe business/contact text

Disallowed:

- arbitrary JavaScript
- raw HTML injection
- remote script tags
- CSS that can escape intended page design
- unvalidated image/data payloads
- dangerous URLs

Important rule:

```text
Custom branding must not become custom JavaScript execution.
```

All branding text must be escaped when rendered.

## Content-Type and File Content

The browser-provided content type is advisory only.

Security requirements:

- do not trust MIME type for security decisions
- store uploads as files, not executable code
- do not execute uploaded content
- avoid serving uploaded files as active HTML/JS in privileged origin
- consider safe download headers for risky content types
- future virus/malware scanning hook may be added

Important rule:

```text
Uploaded file content is untrusted data.
```

## Public Page Security

The public Drop Zone page must be safe to open.

Security requirements:

- no private owner data leak
- no destination path leak
- no token_hash/password_hash leak
- escape branding text
- escape filenames in upload result display
- use safe error messages
- avoid stack traces
- avoid debug info
- avoid exposing internal API details

## Abuse Controls

Because Drop Zone is public, abuse controls are important.

Useful controls:

- expiry
- password
- max file size
- per-link total limit
- per-IP rate limit
- per-token rate limit
- upload session timeout
- admin disable link
- audit logging
- global server limit

Abuse examples:

- storage exhaustion
- repeated password guessing
- many incomplete chunk sessions
- huge upload attempts
- malicious filenames
- upload spam

## Audit and Activity Events

Useful events:

- Drop Zone link created
- Drop Zone link disabled
- Drop Zone link expired attempt
- invalid token attempt
- password failure
- upload started
- upload completed
- upload rejected by quota
- upload rejected by size limit
- upload rejected by invalid destination
- upload rejected by invalid filename
- duplicate policy applied
- public page opened, if desired and privacy policy allows

Audit fields should avoid storing sensitive raw secrets.

Do not log:

- raw token
- plaintext password
- password hash
- private file contents

## Error Handling

Public errors must be safe.

Good examples:

```text
Link not available.
Upload expired.
Upload too large.
Upload failed.
Invalid password or link.
Insufficient storage.
```

Bad examples:

```text
Token hash abc123 not found.
Owner fp d9cf... has no quota.
Failed to write /srv/pqnas/users/alice/private/client/file.pdf.
SQLite query failed with full stack trace.
```

## Interaction With File Access Security

Drop Zone ultimately writes files into normal DNA-Nexus storage.

Therefore it must follow the same storage safety principles as File API:

- normalize destination
- check containment
- enforce quota
- avoid local path leaks
- protect internal metadata
- handle filenames safely
- write atomically where practical

Drop Zone is not allowed to be a shortcut around File API security.

## Checklist for Drop Zone Route Changes

Before changing or adding a Drop Zone route, answer:

1. Is this owner-authenticated or public-token?
2. Does it expose any owner/private metadata?
3. Does it reveal token_hash, password_hash, owner_fp, or destination_path?
4. Does it accept filenames?
5. Does it accept paths?
6. Does it write to temporary storage?
7. Does it enforce expiry?
8. Does it enforce disabled state?
9. Does it enforce password if configured?
10. Does it enforce quota?
11. Does it validate duplicate policy?
12. Does it escape branding and filenames in responses?
13. Does it create useful audit/activity events?
14. What happens if upload is interrupted?
15. What happens if link expires during chunked upload?

## Things We Must Not Break

- Drop Zone must remain one-way.
- Public uploaders must not browse destination contents.
- Public uploaders must not download existing files.
- Public uploaders must not choose raw filesystem destinations.
- Public responses must not leak owner internals.
- Raw tokens/passwords must not be logged or exposed.
- Expired/disabled links must stop accepting uploads.
- Quota must be enforced before finalizing uploads.
- Branding must not become arbitrary JavaScript/HTML execution.
- Temporary upload storage must not bypass final checks.
