# DNA-Nexus Server

**DNA-Nexus Server** is a lightweight, identity-first personal storage and collaboration server built around **device-mediated authentication**, **DNA identity**, and a minimal self-hosted NAS architecture.

DNA-Nexus Server keeps authorization anchored to a DNA fingerprint while supporting multiple browser login methods. QR-based DNA Connect login remains the flagship identity-first flow, classic password login can be used when configured, and OPAQUE provides a zero-knowledge password login path where the plaintext password is processed locally in the browser and is not sent to the server.

The browser is treated as an interface into the fingerprint-backed session model, not as the identity itself.

DNA-Nexus Server is part of the broader **CPUNK / DNA-Nexus ecosystem**, alongside:

- **DNA-Messenger** — identity, secure messaging, and QR-based authentication
- **PQ-SSH** — identity-based SSH access
- **DNA-Nexus Server** — identity-secured storage, sharing, apps, and collaboration

> The core idea is simple:
>
> **Identity is the anchor. The login method is only the entry path.**

---

# What DNA-Nexus Server Is

DNA-Nexus Server is **not a traditional NAS distribution** and it is not trying to become a heavy container platform.

It is a focused, identity-first storage server designed for:

- personal and family storage
- small private groups
- secure file sharing
- workspace collaboration
- app-based NAS features
- lightweight self-hosting
- identity-based access control

The project started as PQ-NAS, but the product identity has moved forward. The public name is now:

> **DNA-Nexus Server**

Some internal paths, service names, package names, or source identifiers may still use `pqnas` during the transition. These are implementation details and may be cleaned up over time.

---

# Key Design Goals

DNA-Nexus Server is designed around a few strong principles:

- one internal identity model based on DNA fingerprints
- multiple login methods converging to the same fingerprint-backed session
- QR / DNA Connect login for device-mediated identity approval
- OPAQUE login for zero-knowledge password authentication
- optional classic password login when explicitly configured
- no silent fallback from one login method to another
- no unnecessary heavyweight services
- no forced cloud dependency
- no complex enterprise stack for simple personal storage
- identity belongs to the user, not the server
- access decisions are tied to DNA fingerprints
- authentication and recovery flows fail closed when incomplete or misconfigured

The goal is to keep the system small, transparent, understandable, and secure by design.

# Core Features

Current DNA-Nexus Server builds include:

- QR-code login via **DNA-Messenger**
- device-mediated login approval
- identity-based authorization using DNA fingerprints
- stateless identity verification flow
- web-based File Manager
- multi-user storage
- admin interface
- user approval flow
- user settings and theme selection
- Btrfs-based storage backend
- storage pools
- background storage operations
- user storage migration
- share links for files
- public file sharing
- external workspace access
- QR-based external workspace invites
- workspace roles such as viewer, editor, and owner
- workspace file browsing, upload, and download
- Drop Zone public upload links
- activity logging
- audit logging
- drive health monitoring using SMART / NVMe tools
- bundled web apps
- app manifest system
- theme-aware UI

DNA-Nexus Server has grown beyond the first demo phase. It is now a serious self-hosted storage platform with a real app model, real sharing flows, and a growing collaboration layer.

---

# Included Apps and Interfaces

DNA-Nexus Server is built as a small platform with bundled apps.

Current and planned app areas include:

- **File Manager** — browse, upload, download, rename, delete, share, and manage files
- **Photo Gallery** — photo browsing, thumbnails, metadata, and gallery views
- **Shares Manager** — manage shared files and links
- **Drop Zone** — one-way public upload links
- **Workspace tools** — shared folders with member roles and external access
- **Echo Stack** — bookmark and web archive app
- **Reel Stack** — video gallery app
- **Music Library** — music browsing and playback
- **Snapshot Manager** — storage snapshot management
- **RAID Manager** — storage/RAID related management tools

Apps are served through the DNA-Nexus app system and can define where they appear, such as the desktop, launcher, or sidebar.

---

# Authentication Model

DNA-Nexus Server keeps one internal identity and authorization model.

The internal identity anchor is the DNA fingerprint. Browser login methods are entry paths into that same fingerprint-backed session model.

Current browser login modes include:

- QR / DNA Connect login - the browser shows a QR code, a trusted mobile device approves the challenge, and the server mints pqnas_session.
- Classic password login - the browser sends username/email and password to the password endpoint, the server verifies the configured password credential, resolves it to a fingerprint, and mints pqnas_session.
- OPAQUE zero-knowledge password login - the browser performs OPAQUE client cryptographic steps locally, the plaintext password is not sent to the server, the server verifies the OPAQUE transcript, resolves the account to a fingerprint, and mints pqnas_session.

All successful browser login methods converge to:

- fingerprint
- pqnas_session
- the same authorization checks
- the same File Manager, app, workspace, quota, role, and sharing model

This means the rest of the server should not need to care whether the session came from QR login, password login, or OPAQUE login.

Mobile trusted devices use their own bearer-token and refresh-token model. Mobile pairing and app tokens are separate from browser login mode and are not replaced by OPAQUE browser login.

Important authentication rules:

- login attempts must never create users
- disabled or pending users must not be enabled by login or recovery
- OPAQUE mode must not silently fall back to classic password login
- password mode must not accidentally expose QR login if QR login is disabled
- QR mode must not accidentally expose password endpoints as unintended login paths
- the login UI must fail closed when the selected login method is not available

The browser is treated as a user interface. The server still resolves access to the internal fingerprint identity.

# Storage Model

DNA-Nexus Server uses a pool-based storage layout, currently built around Btrfs.

Typical runtime structure may look like:

```text
/srv/pqnas
├─ pools/
├─ users/
├─ data/
├─ audit/
└─ metadata/
```

The `pqnas` path name is currently retained for compatibility with existing development builds. It may be renamed or abstracted later as the DNA-Nexus branding transition continues.

Storage pools can represent:

- HDD storage
- SSD landing pools
- archive storage
- backup storage
- user-specific storage roots
- workspace storage areas

Files are stored under server-managed directories and access is controlled through DNA-Nexus authorization logic rather than direct filesystem exposure.

---

# Workspaces

DNA-Nexus Server supports workspace-style collaboration.

Workspaces can be used for:

- shared folders
- small team collaboration
- family folders
- external member access
- file review and exchange
- controlled upload/download access

Workspace members can have roles such as:

- **viewer** — can browse and download
- **editor** — can upload and modify content
- **owner** — can manage the workspace

External workspace access allows invited users to join through a QR-based flow without becoming normal local server users.

---

# Sharing

DNA-Nexus Server supports multiple sharing models.

## File Share Links

Users can create share links for files. These links allow selected files to be opened or downloaded without exposing the entire user account.

## Drop Zone Upload Links

Drop Zone provides one-way public upload pages.

A Drop Zone can be used when someone should be allowed to send files to the server without seeing the destination folder or browsing server content.

Possible uses:

- receiving documents
- collecting photos
- client uploads
- family file drop
- temporary upload links

Drop Zone links can be configured with limits such as expiry, password, destination folder, and upload size policy.

## External Workspace Invites

Workspace owners can invite external users through QR-based access links. External users can be allowed to browse, download, or upload depending on their role.

---

# App Model

DNA-Nexus Server includes a bundled app model.

Apps can define metadata such as:

- app id
- name
- version
- entry point
- API base
- permissions
- category
- icons
- UI surfaces

Apps can appear in different places depending on their manifest, for example:

- desktop
- app launcher
- sidebar

This allows larger primary apps to appear prominently while smaller utility apps can stay in the launcher unless pinned or enabled.

---

# System Requirements

DNA-Nexus Server is intentionally lightweight.

## Runtime Requirements

The server is designed to use very little memory and disk space compared to container-heavy NAS systems.

A fresh DNA-Nexus Server installation remains small, but the exact disk footprint depends on which release package, bundled apps, runtime tools, and system dependencies are installed.

Recent real installation measurements from a fresh Ubuntu Server based install:

| Resource | Usage |
|---|---:|
| Server RAM | ~11 MB RSS |
| DNA-Nexus storage root | ~21 MB |
| Static/runtime assets | ~16 MB |
| Configuration files | ~224 KB |
| Runtime state before user data | ~0 MB |

Example measured paths:

```text
/srv/pqnas        21M
/opt/pqnas        16M
/etc/pqnas        224K
/var/lib/pqnas    0
```

The observed total OS filesystem increase during a full test install was approximately:

```text
Before install: 10.73 GiB used
After install:  12.20 GiB used
Increase:       ~1.47 GiB
```

That larger number includes operating system packages, installer support files, runtime dependencies, package caches, Cloudflare Tunnel tooling when installed, and other host-level components. It is not only the DNA-Nexus application data directory.

Actual usage depends on enabled apps, storage configuration, number of users, background tasks, thumbnails, metadata indexing, archive generation, media previews, and future features.

## Installation Requirements

The installer may use a temporary Python environment for the Textual TUI installer and may install required runtime tools through the host package manager.

Typical temporary or host-level installation space may include:

| Component | Approximate Size |
|---|---:|
| Installer environment | ~40–50 MB |
| DNA-Nexus installed files | ~40 MB before user data |
| System dependencies and package cache | up to ~1–2 GB depending on host state |

Recommended minimum free disk space before installation:

```text
2 GB
```

Recommended comfortable free disk space before installation:

```text
5 GB+
```

For real storage use, the actual storage pool should of course be much larger.

---

# Quick Install

Release packaging may still use transitional `pqnas` naming while the project branding moves to DNA-Nexus Server.

Typical install flow:

```bash
tar -xzf dna-nexus-server-<version>-linux-x86_64.tar.gz
cd dna-nexus-server
sudo ./install.sh
```

During the transition period, some builds may still use package names like:

```bash
tar -xzf pqnas-<version>-linux-x86_64.tar.gz
cd pqnas
sudo ./install.sh
```

The installer guides you through:

- selecting storage
- configuring server settings
- enabling HTTPS when available
- initializing the server
- preparing runtime directories
- starting the service

After installation, the server starts automatically.

---

# Architecture

DNA-Nexus Server is intentionally simple.

Core architecture:

- **C++ backend server**
- **static web UI**
- **bundled app system**
- **Btrfs-based storage support**
- **identity-first authentication**
- **DNA fingerprint based authorization**
- **small runtime footprint**
- **minimal service dependencies**

The project avoids unnecessary runtime complexity. The goal is that the server remains understandable, inspectable, and practical to run on modest hardware.

---

# Documentation Map

The repository documentation is organized by topic under docs/.

Useful entry points:

- docs/README.md - documentation index
- docs/auth/ - login modes, QR authentication, password authentication, mobile authentication, and browser auth notes
- docs/security/ - secure coding baseline, red-team notes, and security guidance
- docs/technical/ - implementation design notes and deep technical documents
- docs/architecture/ - architecture and storage design
- docs/operations/ - operations, troubleshooting, maintenance, and deployment notes
- docs/product/ - product and app-level documentation
- docs/user-guides/ - user-facing guides
- docs/research/ - research notes and exploratory design work

Root-level documentation should stay minimal. New long-form documents should normally go into the most relevant docs/ subdirectory.

# Why Not a Traditional NAS?

Traditional NAS systems often rely on:

- local usernames and passwords
- browser sessions
- large web stacks
- plugin ecosystems with heavy dependencies
- VPN recommendations for safe remote access
- complex admin surfaces

DNA-Nexus Server takes a different approach.

It focuses on:

- identity-first access
- phone-mediated login
- minimal backend design
- strong separation between browser UI and identity proof
- lightweight bundled apps
- simple storage primitives
- practical self-hosting

It is not trying to replace every enterprise NAS feature. It is trying to provide a secure, understandable, identity-first storage system.

---

# Current Status

DNA-Nexus Server is in **active development**.

It is no longer just an early demonstration. The project already includes a working server, authentication flow, file manager, admin interface, storage handling, sharing, workspaces, external access flows, and multiple bundled apps.

At the same time, the project is still evolving. Some APIs, UI flows, internal paths, and package names may change as the system matures.

Current focus areas include:

- polishing the app platform
- improving workspace collaboration
- expanding activity and audit visibility
- improving external sharing flows
- strengthening bundled apps
- improving installer and release packaging
- continuing the transition from PQ-NAS naming to DNA-Nexus Server branding

---

# Non-Goals

DNA-Nexus Server deliberately avoids some traditional NAS directions.

Not primary goals:

- becoming a Kubernetes platform
- becoming a heavy virtualization system
- replacing large enterprise storage clusters
- making traditional password login the only or primary identity model
- depending on a large container stack for normal operation
- hiding the system behind unnecessary complexity

The project may integrate with other tools where useful, but the core server should remain small, direct, and identity-first.

# Security Philosophy

DNA-Nexus Server is built around the idea that identity should not be reduced to a browser session alone.

Important principles:

- identity should belong to the user
- access should be tied to cryptographic identity
- browser login methods should resolve to the same internal fingerprint model
- QR login should use a trusted device as the approval anchor
- OPAQUE login should avoid sending plaintext passwords to the server
- classic password login should be explicit, configured, rate-limited, and fail closed
- compromised browser state should not automatically mean full account compromise
- sharing should be explicit and controlled
- external access should be role-based and limited
- admin, recovery, and onboarding flows should avoid partial unsafe state

> Identity is the anchor.
>
> The browser is only the interface.
>
> The server verifies access instead of blindly trusting UI state.

# Naming Notes

The project was originally called **PQ-NAS**.

The current product name is:

> **DNA-Nexus Server**

During the transition, some of the following may still contain `pqnas`:

- repository name
- binary name
- service name
- install paths
- configuration paths
- source code identifiers
- package artifacts

This is expected during the migration period.

Public-facing documentation, branding, and user-facing language should prefer **DNA-Nexus Server**.

---

# License

Apache License 2.0

---

# Philosophy

> Identity should belong to the user, not the server.
>
> Storage should be private by default, but easy to share intentionally.
>
> The browser is only the interface.
>
> Identity is the anchor.
>
> The login method is only the entry path.
>
> DNA-Nexus Server should stay lightweight, transparent, and understandable.