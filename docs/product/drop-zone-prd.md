# Drop Zone Product Requirements

## Status

Draft.

## Summary

Drop Zone is a one-way public upload link feature for DNA-Nexus Server.

It allows a DNA-Nexus user or organization to create a public upload page where an external person can send files into a selected destination without having a full account and without being able to browse existing server content.

## Product Goal

Make it easy and safe to receive files from outside users.

Drop Zone should be useful for:

- small businesses receiving customer documents
- photographers receiving photos or source material
- families collecting files from relatives
- clubs collecting forms or media
- operators offering DNA-Nexus Server as a private cloud product
- branded client upload pages

## Core Value

Traditional file sharing often focuses on sending files out.

Drop Zone focuses on receiving files in.

```text
Owner creates link.
External person opens link.
External person uploads files.
Owner receives files in the chosen destination.
```

The external uploader should not need a DNA-Nexus account.

## Target Users

### Owner

The owner is a normal DNA-Nexus user who creates the Drop Zone link.

Owner needs:

- choose upload destination
- set optional password
- set expiry
- set upload limits
- share link
- see uploaded files
- optionally see upload activity

### External Uploader

The external uploader is someone outside the server.

Uploader needs:

- open a simple upload page
- understand who/what the upload is for
- select files
- send files
- receive clear success/failure message

Uploader must not need:

- full account
- admin approval
- access to File Manager
- access to destination folder browsing

### Admin

The admin needs visibility and controls.

Admin needs:

- audit logs
- ability to disable abusive links
- global limits if needed
- storage/quota safety
- safe defaults

## Core Requirements

### Link Creation

Owner can create a Drop Zone link.

The link should contain or reference:

- destination folder
- expiry time
- optional password policy
- max upload size
- optional title/description
- optional branding settings
- creation timestamp
- owner identity
- enabled/disabled state

### Upload Page

The public upload page should be simple and clear.

It should show:

- title
- description/instructions
- optional owner/business branding
- file picker
- upload button
- upload progress
- success/failure result

The upload page must not show:

- destination folder listing
- existing files
- private owner information beyond intended branding
- internal filesystem paths
- admin/internal diagnostics

### One-Way Access

Drop Zone is one-way.

External uploaders can upload files, but they cannot browse, download, rename, delete, or inspect destination contents.

This is a core product rule.

### Optional Password

Owner may set an optional password for the Drop Zone link.

Password rules:

- password must not be stored in plaintext
- failed attempts should be rate-limited
- error messages must not reveal sensitive details

### Expiry

Owner may set expiry.

After expiry:

- upload page should clearly say the link has expired
- upload attempts must be rejected server-side
- old links must not silently continue accepting files

### Upload Size Limits

Drop Zone must support upload limits.

Useful limit types:

- per-file max size
- total upload batch max size
- optional server/admin global max
- quota-based limit from owner storage

The server must enforce limits. Frontend-only limits are not enough.

### Quota Safety

Drop Zone uploads must not bypass user quota or storage policy.

Before accepting an upload, the server must verify that the destination owner has enough allowed storage capacity.

Important rule:

```text
No public upload path may bypass quota checks.
```

### Destination Safety

The selected destination must be controlled by the owner and validated by the server.

The server must prevent:

- path traversal
- writing outside allowed storage roots
- writing into another user's private area
- overwriting protected files unexpectedly
- using browser-supplied paths as trusted filesystem paths

### Audit Logging

Drop Zone should write audit/activity events for important actions.

Examples:

- link created
- link disabled
- upload started
- upload completed
- upload rejected
- password failure
- expired link attempt
- quota failure
- invalid path rejection

Audit events should be useful for owner/admin troubleshooting.

### Branding

Drop Zone should support simple branding.

Possible branding fields:

- logo
- title
- description
- accent color
- background style
- footer text
- business/contact text

Branding should be stored as safe structured settings, not arbitrary trusted script execution.

Important rule:

```text
Custom branding must not become custom JavaScript execution.
```

## Non-Goals

Drop Zone is not:

- a full public file manager
- a public download folder
- a replacement for workspace membership
- an anonymous account creation flow
- a way to bypass storage quota
- a way to execute custom scripts
- a federation protocol
- a general website hosting feature

## Security Requirements

Drop Zone is externally reachable by design, so it must be treated as a hostile-input surface.

Required protections:

- unguessable tokens
- token hashing or safe token storage
- expiry checks on the server
- optional password hashing
- upload size enforcement
- quota enforcement
- path normalization
- destination authorization
- content-type handling that does not trust browser headers blindly
- audit logging
- rate limiting or abuse controls where practical

## UX Requirements

The upload page should be more polished than a bare technical form.

Good UX:

- clear page title
- clear upload instructions
- visible upload progress
- mobile-friendly layout
- obvious success message
- helpful error messages
- branded page option for business users

Bad UX:

- generic blank page
- confusing technical errors
- hidden upload limits
- no progress
- no clear confirmation
- exposing internal paths or server implementation details

## Business Value

Drop Zone can be a strong selling point for small businesses and local operators.

A branded upload page allows a business customer to send links that feel like their own service, not like a generic storage tool.

This makes DNA-Nexus Server more interesting than a plain NAS file manager because it becomes a customer-facing tool.

## Future Ideas

Possible later additions:

- per-link branding templates
- per-customer upload forms
- upload notification emails/messages
- virus/malware scanning hook
- file request templates
- public upload inbox view
- automatic folder naming by uploader/date
- operator-level branding defaults
- Drop Zone analytics for business users

## Open Questions

- Should Drop Zone support multiple destination folders per link?
- Should uploader be able to add a name/email/message?
- Should the owner receive notifications after upload?
- Should branding be per-link, per-user, per-organization, or all three?
- Should admin be able to set global Drop Zone defaults?
- Should old expired links be kept for audit history or cleaned after a retention period?
