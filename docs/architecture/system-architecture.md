# DNA-Nexus Server System Architecture

## Purpose

This document describes the high-level architecture of DNA-Nexus Server.

DNA-Nexus Server is an identity-first personal storage and collaboration server. It is designed to stay lightweight, understandable, and practical to self-host while still supporting file storage, sharing, bundled apps, workspaces, public upload links, and future federation features.

The top-level `README.md` remains the public product overview. This document focuses on how the system is structured internally.

## Core Idea

DNA-Nexus Server is built around a simple principle:

> The browser is only the interface. The trusted device proves identity.

The server should not rely on traditional password login as the main trust model. Authentication and authorization are tied to DNA identity and device-mediated approval flows.

## Major System Areas

```text
DNA-Nexus Server
├─ Core HTTP backend
├─ Authentication and session flow
├─ User and admin management
├─ File storage and storage pools
├─ Workspace collaboration
├─ Sharing and public access flows
├─ Bundled app platform
├─ Activity and audit logging
├─ Background jobs
├─ Static web UI
└─ Federation research/integration layer
```

## Core Backend

The backend is the main server process. It provides HTTP routes, API endpoints, session handling, user management, storage operations, app serving, and admin functions.

Important responsibilities:

- serve the main web UI
- expose API routes
- verify authentication state
- enforce authorization
- manage users and admins
- manage storage pools
- handle file operations
- serve bundled app assets
- write audit/activity events
- run background operations where needed

The server should remain small and direct. Avoid turning the core server into a large framework or plugin runtime unless there is a clear product reason.

## Authentication and Identity

DNA-Nexus Server uses an identity-first authentication model.

Typical login concept:

1. Browser requests access.
2. Server displays a QR/challenge flow.
3. Trusted device approves the login.
4. Trusted device signs or proves the challenge.
5. Server verifies the proof.
6. Browser receives access only after server-side approval.

Important security principle:

```text
The browser is not the root of trust.
```

The browser should be treated as a UI surface. The identity proof should come from the trusted device / DNA identity model.

## Storage Model

DNA-Nexus Server stores user files under server-managed paths.

The storage layer is responsible for:

- user storage roots
- storage pools
- Btrfs-backed storage behavior
- quota checks
- workspace storage
- file upload/download operations
- background file operations
- storage migration
- future archive/cache growth

Storage must not be exposed directly through raw filesystem paths. API routes must normalize and authorize paths before performing file operations.

Important rule:

```text
A browser-supplied path is never automatically trusted.
```

## Bundled App Platform

DNA-Nexus Server includes bundled apps.

Examples:

- File Manager
- Photo Gallery
- Shares Manager
- Drop Zone
- Echo Stack
- Reel Stack
- Music Library
- Snapshot Manager
- RAID Manager
- Circle Stack

Bundled apps are served through the app system. Apps can define metadata and UI placement in their manifests.

Important app manifest concepts:

- app id
- name
- version
- entry point
- API base
- permissions
- category
- icons
- UI surfaces

UI surfaces allow apps to appear in different places:

```text
desktop
launcher
sidebar
```

Primary apps can be visible in the sidebar. Smaller utility apps can stay in the launcher or desktop without cluttering the main navigation.

## Sharing and External Access

DNA-Nexus Server supports multiple sharing models.

### File Share Links

File share links expose selected files without exposing the full account or folder.

### Drop Zone Upload Links

Drop Zone links allow external users to upload files into a destination selected by the owner.

Drop Zone is one-way:

```text
External uploader can send files.
External uploader cannot browse existing files.
External uploader cannot download destination folder contents.
```

### External Workspace Invites

Workspace owners can invite external users into a controlled workspace flow. Access depends on role.

Typical roles:

- viewer
- editor
- owner

## Workspaces

Workspaces provide shared collaboration areas.

They can be used for:

- family folders
- small team collaboration
- client folders
- external access
- controlled upload/download workflows

Workspace access must be role-based and enforced by the backend.

## Activity and Audit Logging

DNA-Nexus Server should record important security and activity events.

Examples:

- login attempts
- login approvals
- denied access
- disabled user flows
- admin actions
- file share creation
- Drop Zone upload activity
- workspace membership changes
- storage/admin operations

Audit logging is not only for debugging. It is part of the security and operations model.

## Federation Layer

Circle Stack federation uses Nodus as a research/integration layer.

Current principle:

```text
Nodus carries federation events and safe references.
Media remains on the origin DNA-Nexus Server.
```

This keeps large user media out of the federation event layer and allows the origin server to remain responsible for serving its own media according to its own rules.

The current known-good research notes are in:

```text
docs/research/circle_stack_nodus_federation.md
```

## Runtime Path Naming

Some runtime paths, service names, source identifiers, and package names still use `pqnas`.

This is expected during the transition from PQ-NAS to DNA-Nexus Server.

Public-facing language should prefer:

```text
DNA-Nexus Server
```

Internal compatibility paths may continue to use:

```text
pqnas
```

until there is a dedicated migration plan.

## Design Constraints

DNA-Nexus Server should remain:

- lightweight
- inspectable
- understandable
- identity-first
- private by default
- explicit about sharing
- careful with external access
- practical to run on modest hardware

Avoid unnecessary complexity such as:

- heavy container dependency as the normal model
- Kubernetes-style platform assumptions
- browser-only trust
- password-first login as the main identity model
- exposing raw filesystem paths to users
- app behavior that bypasses quota/security checks

## Things We Must Not Break

- Browser must not become the root of trust.
- File paths must stay server-normalized and authorized.
- External users must not get more access than their link or role allows.
- Drop Zone must remain one-way.
- App manifests must control app visibility cleanly.
- Quota checks must happen before large archive/upload operations are accepted.
- Public/private visibility rules must remain explicit.
- Federation events must not carry large media files.
- Existing `pqnas` runtime compatibility must not be renamed casually.
