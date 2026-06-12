# DNA-Nexus Server Documentation

This directory contains internal and public-facing documentation for DNA-Nexus Server.

The top-level repository `README.md` remains the main public overview. The documents under `docs/` are more specific and are intended to capture product requirements, architecture, technical design decisions, security assumptions, operations notes, and user/admin guidance.

## Documentation Map

### Product documents

Product documents describe what is being built, why it exists, who it is for, and what is intentionally out of scope.

- `product/drop-zone-prd.md` — Product requirements for Drop Zone public upload links.

### Architecture documents

Architecture documents describe how the main system parts fit together.

- `architecture/system-architecture.md` — High-level DNA-Nexus Server system architecture.

### Technical documents

Technical documents describe implementation-level details such as APIs, database tables, storage behavior, deployment steps, and coding contracts.

Planned documents:

- `technical/api-overview.md`
- `technical/database-schema.md`
- `technical/app-manifest-model.md`
- `technical/build-and-deploy.md`
- `technical/static-files-and-bundled-apps.md`

### Security documents

Security documents describe trust boundaries, threat models, security assumptions, and known hardening work.

Existing documents:

- `security/file-access-security.md` — File access trust boundaries, path safety, quota, shares, versions, previews, and upload security.
- `security/drop-zone-security.md` — Drop Zone owner/public-token surfaces, upload safety, quota, expiry, branding, and abuse controls.

Planned documents:

- `security/threat-model.md`
- `security/authentication-and-sessions.md`

### ADR documents

ADR means Architecture Decision Record. These are short documents that capture important technical or product decisions.

Planned documents:

- `adr/0001-documentation-structure.md`
- `adr/0002-app-manifest-surfaces.md`
- `adr/0003-drop-zone-one-way-upload.md`
- `adr/0004-circle-stack-media-origin-fetch.md`

### User and admin guides

User guides explain how normal users and administrators operate DNA-Nexus Server.

Planned documents:

- `user-guides/admin-guide.md`
- `user-guides/user-guide.md`
- `user-guides/drop-zone-guide.md`

### Operations documents

Operations documents explain installation, deployment, upgrades, backups, runtime paths, and service management.

Planned documents:

- `operations/install-and-upgrade.md`
- `operations/runtime-paths.md`
- `operations/backup-and-restore.md`

### Research documents

Research documents capture experimental work, spike results, smoke tests, and known-good checkpoints.

Existing document:

- `research/circle_stack_nodus_federation.md`

## Writing Rules

Keep documentation practical.

Each document should answer:

1. What is this?
2. Why does it exist?
3. Who uses it?
4. How does it work?
5. What are the important constraints?
6. What should not be broken?

Avoid duplicating the top-level `README.md`. If the README already explains the public product story, the docs here should go deeper into product requirements, architecture, implementation, security, or operations.

Prefer small focused documents over one huge document.
