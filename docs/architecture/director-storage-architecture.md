# DNA-Nexus Director / Storage Node Architecture

## Status

Active architecture contract.

This document describes the intended trust model and responsibility split
between:

- standalone DNA-Nexus / PQ-NAS installations
- Director
- Storage Nodes

It also records the current implementation status while the cluster storage
architecture is being introduced.

This document is intended to be read before changing authentication, user
placement, Storage Node sessions, storage projections, user-root resolution,
or cluster-aware file routes.

Related documents:

```text
docs/architecture/system-architecture.md
docs/architecture/storage/storage-tiering.md
docs/security/secure_coding_baseline.md
docs/security/file-access-security.md
docs/auth/dna-nexus-pqnas-auth-methods.md
```

---

# 1. Core Design Principle

The same server codebase must support two different deployment models:

```text
Standalone
```

and:

```text
Director + Storage Node cluster
```

Standalone support is a first-class requirement.

Existing local user, authentication, and file functionality must therefore
not be removed merely because cluster mode exists.

The important distinction is not:

```text
legacy code vs new code
```

but:

```text
which authority and route wiring is valid for the active server role
```

---

# 2. Supported Server Roles

The server may operate conceptually in three roles:

```text
Standalone
Director
Storage Node
```

They have different trust responsibilities.

---

# 3. Standalone Mode

## DESIGN CONTRACT

A standalone installation is self-contained.

It may locally own:

- user registry
- admin registry
- browser authentication
- OPAQUE credentials
- password credentials where configured
- DNA / trusted-device authentication state
- user sessions
- storage pool assignment
- user file roots
- File API routes
- storage management
- bundled applications

Conceptually:

```text
Browser
   |
   v
Standalone server
   |
   +-- authenticate user
   +-- authorize user
   +-- resolve storage
   +-- access files
```

Standalone mode is allowed to use the existing local user and authentication
infrastructure.

Examples include local files such as:

```text
users.json
opaque_credentials.json
opaque_server_setup.bin
```

when the selected authentication configuration requires them.

## IMPORTANT COMPATIBILITY RULE

Cluster work must not remove standalone functionality.

Code used by standalone installations may remain in the shared server binary.

The requirement is that cluster roles must not incorrectly use standalone
authorities.

---

# 4. Director Role

## DESIGN CONTRACT

The Director is the control-plane authority for a cluster installation.

The Director owns or coordinates:

- user identity
- browser login
- user registry
- user enabled/disabled state
- administrative user management
- Storage Node registry
- Storage Node enrollment
- Storage Node health
- user storage placement
- home-node assignment
- storage pool assignment
- storage projection generation
- projection revision advancement
- synchronization of projections to Storage Nodes
- creation of Storage handoff authorization

Conceptually:

```text
                         +----------------------+
Browser ---> Director -->| identity / control   |
                         | plane                |
                         +----------+-----------+
                                    |
                                    | trusted internal
                                    | cluster communication
                                    v
                         +----------------------+
                         | Storage Node         |
                         | data plane           |
                         +----------------------+
```

The Director is the authoritative source for determining whether a cluster
user is entitled to use a particular Storage Node.

The Director must not delegate user-authority decisions to arbitrary local
state on a Storage Node.

---

# 5. Storage Node Role

## DESIGN CONTRACT

A Storage Node is a data-plane server.

Its job is to store and serve data for users assigned to it.

A Storage Node may own:

- storage pools
- user data roots
- storage metadata
- tiering state
- storage projections received from Director
- Storage-scoped user session state
- Storage Node operational state

A Storage Node is not the cluster's primary user authority.

It must not decide cluster user identity or placement from a standalone-style
local user registry.

Conceptually:

```text
Storage Node
   |
   +-- verify Storage-scoped request authentication
   +-- verify current user projection
   +-- verify exact projection revision
   +-- verify local home-node assignment
   +-- resolve assigned pool
   +-- resolve user root
   +-- authorize path
   +-- operate on file
```

---

# 6. Shared Binary Does Not Mean Shared Authority

The same `pqnas_server` binary may contain implementations needed by:

```text
Standalone
Director
Storage Node
```

This is intentional.

For example, standalone user-management and authentication code may exist in
the same binary installed on a Storage Node.

That does not mean Node role is allowed to use that code as its user
authority.

Correct behavior is role-dependent registration and composition.

Conceptually:

```cpp
if (standalone) {
    register_standalone_auth_and_file_routes();
}

if (director) {
    register_director_control_plane_routes();
}

if (node) {
    register_projection_backed_storage_routes();
}
```

The exact implementation does not need to use these function names.

The architectural requirement is the separation of authority.

---

# 7. User Placement

## DESIGN CONTRACT

A cluster user must have explicit storage placement before a Storage Node may
serve that user's normal files.

Relevant placement concepts include:

```text
storage state
home node
storage pool
projection revision
```

A typical allocated user is conceptually:

```text
user
  fingerprint = <identity>
  storage_state = allocated
  home_node = storage-r230-01
  pool_id = default
  projection_revision = N
```

An unallocated user must not silently acquire a Storage Node or pool through
fallback behavior.

Example:

```text
storage_state = unallocated
home_node = ""
pool_id = ""
```

means there is no valid Storage assignment.

---

# 8. Storage User Projection

## DESIGN CONTRACT

The Director sends Storage Nodes only the user state needed to authorize and
locate data on that node.

This reduced state is called a Storage user projection.

A projection is not a copy of the Director's complete user database.

Storage Nodes should not require normal Director user secrets merely to serve
files.

A projection must be sufficient to establish at least:

- user identity anchor
- enabled state
- allocated storage state
- current home-node assignment
- storage pool assignment
- authorization role required by Storage
- non-zero projection revision

The exact serialized schema is defined by the implementation.

---

# 9. Projection Revisions

Projection revisions are part of the security boundary.

They are not merely synchronization metadata.

## DESIGN CONTRACT

A Storage request authenticated against projection revision:

```text
N
```

must resolve its user root against that same revision:

```text
N
```

It must not authenticate against revision N and then perform the filesystem
operation using a newer or older projection snapshot.

Conceptually:

```text
authenticate request
       |
       v
projection revision = N
       |
       v
resolve user root at revision N
       |
       +-- revision still N --> continue
       |
       +-- revision changed --> reject
```

A stale or mismatched revision must fail closed.

This prevents placement changes from racing authentication and filesystem
resolution.

---

# 10. Projection Synchronization

## DESIGN CONTRACT

The Director reconciles authoritative user placement state with Storage Node
projection state.

Conceptually:

```text
Director users / placement
          |
          v
build desired projection
          |
          v
compare sync state
          |
          v
authenticated internal POST
          |
          v
Storage projection store
```

The current internal Storage projection endpoint is:

```text
POST /internal/storage/user-projection
```

It must require authenticated internal cluster communication.

Unauthenticated requests must fail closed.

---

# 11. Director Projection Sync State

The Director persists projection synchronization progress separately from
ordinary user state.

Current production path:

```text
/srv/pqnas/config/storage_projection_sync_state.json
```

Initial version-1 state:

```json
{
  "states": [],
  "version": 1
}
```

## SECURITY CONTRACT

On an established production Director, a missing synchronization state file
must not be silently interpreted as an empty new installation.

Initial creation is an explicit bootstrap operation.

Bootstrap must be:

- intentional
- create-only
- non-overwriting
- restrictive in file permissions

An existing sync-state file must never be automatically replaced with an
empty state.

This prevents loss of synchronization history from being mistaken for a
fresh deployment.

---

# 12. Storage Projection Store

The Storage Node maintains projections received from the Director.

Current production path:

```text
/srv/pqnas/config/storage_user_projections.json
```

The file may not exist before the first persisted projection, depending on
the current store implementation.

The absence of a projection for a user must never grant access.

No projection means no cluster Storage authorization for that user.

---

# 13. Director-to-Storage Internal Authentication

## DESIGN CONTRACT

Internal cluster endpoints are not ordinary public APIs.

They require authenticated node-to-node communication.

Examples include:

```text
/internal/node/status
/internal/storage/user-projection
```

Unauthenticated access must fail closed.

The reverse proxy must not bypass this authentication requirement.

Cluster-internal credentials must not be exposed to browsers.

---

# 14. Browser Authentication and Storage Authentication

Director browser authentication and Storage Node file access are separate
trust domains.

## DESIGN CONTRACT

A browser session accepted by Director must not automatically become a valid
Storage Node session merely because the user has the same fingerprint.

In particular, Node role must not use a normal Director browser cookie as a
fallback authentication mechanism for cluster file access.

Node role must also not silently fall back to unrelated bearer
authentication when a Storage-scoped session is required.

The intended cluster flow is:

```text
Browser
   |
   | Director authentication
   v
Director
   |
   | controlled Storage handoff
   v
Storage Node
   |
   | Storage-scoped session
   v
user file request
```

This gives Storage its own request-authentication boundary while still using
Director as the user and placement authority.

---

# 15. Storage Handoff

## DESIGN CONTRACT

The Director-to-Storage browser handoff must bind access to the intended:

- user
- Storage Node
- current placement
- current projection revision
- validity period
- one-time or otherwise replay-controlled handoff semantics as defined by the
  implementation

A Storage Node must not mint normal cluster user access merely from
browser-controlled identity claims.

## CURRENT STATUS

The handoff foundation exists in the codebase.

Production handoff wiring is intentionally still closed while the complete
user-file trust chain is being completed and tested.

Production registration must remain closed until all required authentication,
projection, revision, root-resolution, and route-wiring guarantees are in
place.

---

# 16. Storage User Access Runtime

## IMPLEMENTED NOW

Node role composes a Storage user access runtime after required Storage
configuration and key material are available.

The runtime binds together the trusted components required for Storage user
access, including:

- Storage projection store
- Node-specific authentication key material
- local Node ID
- strict storage-pool root resolution
- authentication clock

This runtime is intended to prevent individual routes from independently
reconstructing weaker authentication or storage-resolution logic.

---

# 17. Strict Pool Resolution

## DESIGN CONTRACT

A projected pool ID must resolve to that exact configured pool.

A named pool that cannot be resolved must not silently fall back to another
pool such as `default`.

Conceptually:

```text
projection says pool A
       |
       +-- pool A exists --> use pool A
       |
       +-- pool A missing --> reject
```

Never:

```text
projection says pool A
pool A missing
       |
       v
use default pool
```

Silent fallback could redirect one user's access into an unintended storage
root.

---

# 18. User Root Resolution

In cluster Node role, a user's storage root is derived from trusted placement
state rather than browser input.

Conceptually:

```text
projection
   |
   +-- fingerprint
   +-- pool_id
   +-- home_node
   +-- revision
          |
          v
strict pool resolver
          |
          v
<pool data root>/users/<fingerprint>
```

A browser must never supply an absolute user root.

A Storage Node must verify that the projection's home node is the current
local node before using it for normal user access.

---

# 19. Filesystem Path Containment

Storage placement authorization does not replace filesystem path security.

Both are required.

Conceptually:

```text
valid Storage session
        +
valid current projection
        +
valid user root
        +
safe contained path
        =
allowed filesystem operation
```

Existing-path operations must use canonical containment where required to
prevent symlink-based escape.

The general file-access rules in:

```text
docs/security/file-access-security.md
```

remain applicable.

Known limitation:

Canonical path checking alone does not make every filesystem operation fully
race-free against filesystem TOCTOU attacks.

More race-resistant directory-handle/openat-style designs may be future
hardening work where required.

---

# 20. Storage Tiering Is a Lower Layer

Cluster placement and Storage tiering solve different problems.

Cluster architecture decides:

```text
which Storage Node and pool owns the user's data
```

Storage tiering decides:

```text
where inside that Storage system a particular file currently resides
```

Conceptually:

```text
Director placement
      |
      v
Storage Node + pool
      |
      v
user logical storage root
      |
      v
tiering subsystem
      |
      +-- landing tier
      +-- migrating
      +-- capacity tier
```

See:

```text
docs/architecture/storage/storage-tiering.md
```

for tiering behavior.

---

# 21. Local Legacy Files on Storage Nodes

Because the server supports standalone installations and uses a shared
codebase, a machine currently operating as Node may still contain files such
as:

```text
users.json
opaque_credentials.json
opaque_server_setup.bin
```

especially during migration from earlier standalone installations.

Their mere presence is not itself an architectural violation.

## SECURITY CONTRACT

When the server role is Node, those files must not become the authoritative
source for normal cluster user file access.

Node cluster routes must use the Storage projection and Storage user access
runtime defined for Node role.

Standalone mode remains allowed to use its local user/authentication state.

---

# 22. Route Wiring Rule

Every user file route must be evaluated for its active server role.

A route that is secure in standalone mode is not automatically secure in Node
mode.

For Node role, normal user file routes must eventually use:

```text
Storage-scoped request authentication
        |
        v
authenticated fingerprint + projection revision
        |
        v
Storage user root resolver at exact revision
        |
        v
safe path containment
        |
        v
filesystem operation
```

A route must not mix:

```text
Storage authentication
```

with:

```text
legacy standalone root resolution
```

or the reverse.

---

# 23. Current Implementation Status

This section describes the state of the current cluster implementation.

It is not the permanent architectural contract.

## IMPLEMENTED NOW

The current implementation includes:

- Director and Node runtime roles
- Storage Node enrollment state
- Director Node registry
- Director heartbeat worker
- protected internal Node status route
- protected internal Storage user projection route
- Storage user projection store
- Director projection synchronization state store
- Director projection reconciliation worker
- explicit first-deployment Director sync-state bootstrap
- Storage user request authenticator
- projection revision propagation from authentication
- exact-revision Storage user root resolution
- strict projected pool resolution
- Storage user access runtime composition
- canonical existing-path containment helper
- role-aware Storage archive/blob route wiring
- production guards preventing premature handoff activation

Live staging has confirmed that:

- Director starts with the new projection reconciliation worker
- Node starts with Storage user access runtime composed
- internal Node status rejects unauthenticated access
- internal Storage projection POST rejects unauthenticated access
- reverse-proxy access does not bypass internal authentication
- Director sees the enrolled Storage Node
- Director and Storage binaries run successfully with the new cluster code

## NOT YET WIRED / NOT YET COMPLETE

At the time this document was created:

- production Storage browser handoff remains closed
- production handoff consume remains closed
- production handoff preauthentication remains closed
- not all user File API routes have been migrated to Node projection-backed
  authorization
- file-version read/manage/restore paths still require role-wiring review
- remaining routes using standalone `UsersRegistry` assumptions must be
  reviewed for Node role
- the first complete allocated-user projection synchronization has not yet
  been exercised end-to-end
- the final browser -> Director -> Storage session -> file access flow has not
  yet been declared production-ready

Do not interpret the presence of handoff-related code as proof that production
handoff is enabled.

---

# 24. Expected End-to-End Cluster Flow

The intended completed flow is:

```text
1. User authenticates to Director
             |
             v
2. Director identifies fingerprint
             |
             v
3. Director checks user is enabled
             |
             v
4. Director resolves storage placement
             |
             +-- allocated?
             +-- home node?
             +-- pool?
             +-- revision?
             |
             v
5. Director ensures current projection exists on Storage
             |
             v
6. Director creates controlled Storage handoff
             |
             v
7. Browser presents handoff to assigned Storage
             |
             v
8. Storage validates handoff against local projection
             |
             v
9. Storage creates Storage-scoped session
             |
             v
10. Browser requests file operation
             |
             v
11. Storage authenticates request
             |
             v
12. Storage obtains exact projection revision
             |
             v
13. Storage resolves user root at that revision
             |
             v
14. Storage validates requested path containment
             |
             v
15. Storage performs filesystem operation
```

Any broken security dependency must fail closed.

---

# 25. Placement Change Behavior

Placement changes must invalidate stale assumptions.

Examples:

```text
user disabled
home node changed
pool changed
storage deallocated
projection revision advanced
```

After such a change, an old Storage session or old request context must not
continue accessing files under stale placement assumptions.

Projection revision checking is one mechanism enforcing this boundary.

---

# 26. Failure Rules

The cluster design prefers explicit failure over unsafe fallback.

Examples:

```text
missing projection            -> reject
stale projection revision     -> reject
wrong home node               -> reject
unknown projected pool        -> reject
invalid Storage session       -> reject
Director cookie on Node route -> reject
unauthenticated internal POST -> reject
path outside user root        -> reject
```

Do not replace these failures with convenience fallback behavior.

---

# 27. Trust Boundaries

## Browser

Untrusted.

May provide:

- paths
- filenames
- request parameters
- cookies
- handoff material
- file content

Browser input must never define server authority by itself.

## Director

Trusted cluster control plane.

Compromise of Director is high impact because Director controls:

- identity
- user enablement
- placement
- projection generation
- handoff authorization

## Storage Node

Trusted for data assigned to that node.

A compromised Storage Node may expose data physically stored on that node.

The architecture should avoid unnecessarily increasing that blast radius by
copying unrelated Director authentication secrets or all-cluster credentials
to every Storage Node.

## Internal Director / Node Channel

Authenticated trust boundary.

It must not be treated as an anonymous LAN-only interface.

---

# 28. Secret-Minimization Goal

## DESIGN CONTRACT

Storage Nodes should receive only secrets and state required for their role.

A Storage Node should not require a complete copy of Director's normal user
authentication database merely to serve cluster files.

Where practical, avoid placing on Storage Nodes:

- normal Director browser credential databases
- unnecessary OPAQUE credential material
- unrelated user authentication secrets
- raw Director signing secrets
- authority for users assigned to other nodes

Standalone compatibility may mean related code or historical files exist on a
machine.

Role usage, not binary contents alone, defines the security boundary.

---

# 29. Things We Must Not Break

The following are architecture invariants.

1. Standalone installations must continue to support local authentication,
   users, storage, and File API behavior.

2. Shared standalone code must not accidentally become Node-role user
   authority.

3. Director remains the cluster authority for identity, user state, and
   storage placement.

4. Storage Node normal user access must depend on a valid local projection.

5. Storage Node must verify that the user is currently assigned to that node.

6. Storage Node must use the exact authenticated projection revision when
   resolving the user's root.

7. A missing or stale projection must fail closed.

8. A named projected pool must not silently fall back to another pool.

9. Director's ordinary browser session must not automatically be accepted as
   Storage Node authorization.

10. Internal cluster APIs must require authentication.

11. Browser-supplied filesystem paths must never become trusted absolute
    paths.

12. File operations must remain contained inside the authorized user or
    workspace root.

13. Production handoff must remain closed until the complete trust chain is
    ready.

14. Director projection sync state must never be silently reset to empty on an
    established installation.

15. Storage tiering must not bypass user-root or cluster-placement
    authorization.

---

# 30. Documentation Maintenance Rule

Changes that alter any of the following should update this document in the
same pull request:

- server role responsibilities
- cluster authentication
- Director-to-Storage trust
- projection schema or semantics
- projection revision behavior
- placement rules
- Storage handoff
- Storage session behavior
- pool resolution
- user-root resolution
- cluster-aware File API route wiring
- standalone compatibility assumptions

Security-sensitive implementation work should be reviewed together with:

```text
docs/security/secure_coding_baseline.md
docs/architecture/director-storage-architecture.md
```

The architecture document defines what trust relationships are allowed.

The secure coding baseline defines how those relationships must be
implemented safely.
