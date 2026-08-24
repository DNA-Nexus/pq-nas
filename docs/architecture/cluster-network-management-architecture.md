# DNA-Nexus Cluster Network and Management Architecture

## Status

Active architecture contract.

This document defines the deployment network model for DNA-Nexus Director / Storage clusters. It complements:

```text
docs/architecture/director-storage-architecture.md
docs/adr/0005-cluster-network-and-remote-management.md
docs/security/secure_coding_baseline.md
```

The goal is to keep home-lab, pilot, and customer deployments structurally consistent even when their IP ranges, DNS, firewall products, or VPN technologies differ.

---

# 1. Three Network Planes

Every cluster deployment has three logically separate planes:

```text
PUBLIC / USER
CLUSTER / CONTROL
MANAGEMENT / SUPPORT
```

They may share physical switches or interfaces in a small lab, but their trust purposes remain separate.

---

# 2. Public / User Plane

The public plane carries intentionally exposed product traffic such as normal browser/mobile access and other public-facing services.

Public ingress may use customer load balancers, reverse proxies, CDN services, or other approved infrastructure where appropriate.

Public ingress architecture MUST NOT silently become the trust path used by authenticated Director-to-Storage cluster communication.

---

# 3. Cluster / Control Plane

The cluster plane carries authenticated Director-to-Storage control traffic, including operations such as:

- heartbeat/status
- Storage user projection synchronization
- node credential rotation
- future authenticated internal cluster operations

## Required path

```text
Director
   |
   | private HTTPS
   v
Storage Node TLS endpoint :443
   |
   | local proxy / loopback only where used
   v
pqnas_server
```

Director MUST reach the Storage Node directly over private/internal routing.

The Storage cluster hostname MUST resolve from Director to the destination Storage Node's private/cluster address, not to:

- the Director itself
- a public CDN
- an Internet hairpin address
- an unrelated reverse proxy
- an unrelated load balancer
- another node

## TLS ownership

The authenticated TLS session used for cluster traffic MUST terminate on the destination Storage Node's trusted local boundary.

Each Storage Node owns a unique TLS private key.

RT014 SPKI pinning therefore means:

```text
expected pin == destination Storage Node TLS public key identity
```

and never:

```text
expected pin == Director proxy / CDN / unrelated TLS terminator
```

The Director requires all of the following for authenticated Storage transport:

```text
CA verification
hostname verification
exact Storage SPKI pin
cluster protocol credential / authorization
```

Pinning strengthens normal TLS verification; it does not replace it.

---

# 4. Naming and Addressing

Each node should have a stable cluster hostname mapped to its private address by customer/internal DNS or an equivalent controlled local mapping.

Conceptual example:

```text
director-01.cluster.customer.example  -> private Director address
storage-01.cluster.customer.example   -> private Storage address
storage-02.cluster.customer.example   -> private Storage address
```

The exact customer DNS suffix is deployment-specific.

Public names and cluster names SHOULD be different when doing so avoids routing ambiguity. If the same logical name is used in multiple views, split-horizon resolution MUST still resolve Director cluster traffic directly to the Storage Node and must not change the TLS trust endpoint.

---

# 5. Cluster Certificates

Each Storage Node MUST have its own private key and certificate identity.

Preferred default when no customer PKI is available:

```text
customer / cluster-specific CA
    |
    +-- Director certificate where needed
    +-- Storage-01 certificate
    +-- Storage-02 certificate
    +-- Storage-03 certificate
```

Customer PKI is acceptable when it satisfies the same requirements.

Requirements:

- node private keys are unique
- node private keys are generated on the node where practical
- private keys are not copied between nodes
- Director/public proxy private keys are not reused for Storage identities
- certificate SANs match the cluster hostname used by Director
- certificate renewal must preserve or deliberately rotate node identity according to the TLS identity lifecycle

A single private CA key shared operationally across unrelated customers is not the default model. Customer compromise must not become an identity compromise for another customer.

---

# 6. Storage Administrative UI

Storage Nodes are directly administrable from an authorized management network.

This is a deliberate product/operations requirement, not a temporary lab convenience.

Reasons include:

- initial installation before Director enrollment
- initial administrator creation/configuration
- Storage-local configuration
- diagnostics and repair
- recovery while Director is unavailable

Conceptually:

```text
Authorized admin browser
        |
        | management HTTPS
        v
Storage Node :443
        |
        +-- administrative UI
```

The same Storage TLS endpoint may also carry authenticated cluster APIs when route authentication remains correctly separated.

Storage administration MUST NOT require traffic to traverse Director merely because Director exists.

---

# 7. Remote Management and Vendor Support

Remote administration is expected for customer sites where physical travel is impractical.

Remote access enters through the management plane, not by exposing Storage management services directly to the Internet.

Compliant approaches may include:

- customer VPN
- dedicated support VPN
- private access gateway
- bastion / jump host
- equivalent customer-approved zero-trust private access solution

The product architecture does not mandate a single vendor or protocol.

## Required guarantees

Remote support SHOULD use MFA and MUST preserve:

- authenticated individual operator identity
- least privilege
- customer-specific isolation
- auditable access
- controlled reachability to Director/Storage management endpoints
- revocability

Customer A support connectivity MUST NOT automatically create network reachability into Customer B.

Where required by customer policy, vendor support access should be capable of operating as:

```text
disabled normally
      |
customer enables bounded support window
      |
vendor support session
      |
automatic expiry / explicit close
```

Persistent vendor access is a deployment/customer-policy choice, not an architectural requirement.

---

# 8. SSH

SSH is an operational management mechanism, not part of the application cluster protocol.

When SSH is enabled:

- it should be reachable only from approved management networks or bastions
- public Internet exposure is not the normal design
- public-key authentication is preferred
- root login should remain disabled
- access should be attributable to an individual administrator where practical

A customer may choose to disallow vendor SSH while still permitting browser-based support or customer-operated diagnostics.

---

# 9. Firewall Model

Conceptual Storage policy:

```text
SOURCE                         DESTINATION             RESULT
----------------------------------------------------------------
Director / cluster network     Storage HTTPS :443      ALLOW
Admin / management network     Storage HTTPS :443      ALLOW
Approved management/bastion    Storage SSH             ALLOW if needed
Public Internet                Storage admin/SSH       DENY
Unrelated customer networks    Storage management      DENY
```

Exact rules depend on customer addressing and security policy.

The important invariant is that reachability is intentional by plane.

---

# 10. Bootstrap Sequence

A standard Storage deployment should converge on this sequence:

```text
1. Install Storage role
2. Configure private cluster/management connectivity
3. Configure stable cluster hostname
4. Generate Storage TLS private key
5. Issue/install Storage TLS certificate
6. Start Storage HTTPS endpoint
7. Access Storage UI from authorized management network
8. Perform required initial admin/configuration
9. Enroll node with Director
10. Director binds node TLS SPKI identity
11. Verify heartbeat and other authenticated control-plane operations
12. Enable normal production service
```

The exact installer/UI workflow may improve over time, but the trust ordering should remain recognizable.

---

# 11. Recovery Contract

Failure of Director MUST NOT automatically remove the authorized administrator's ability to reach a Storage Node for diagnostics.

Conceptually:

```text
Director unavailable
        X
        |
Admin management path ------> Storage UI / approved SSH
```

This direct management path is a recovery feature and must not become an alternate route for normal cluster authorization decisions.

Storage role still obeys the Director/Storage authority rules described in `director-storage-architecture.md` for cluster users.

---

# 12. Lab Parity Rule

The home lab should model the same trust layout used for customers.

It is acceptable for a lab to use one physical LAN rather than separate VLANs, but the logical paths must still be:

```text
Director -> direct Storage HTTPS for cluster traffic
Admin    -> direct Storage HTTPS for administration
Public   -> separate public ingress path
```

Do not create a lab-only proxy arrangement that changes which TLS identity RT014 authenticates.

---

# 13. Deployment Validation Checklist

Before enabling RT014 or declaring a new cluster ready, verify at minimum:

```text
[ ] Director resolves each Storage cluster hostname to the intended private node address
[ ] Director connects directly to Storage :443
[ ] no public CDN/reverse proxy terminates authenticated cluster TLS
[ ] Storage certificate hostname validation succeeds
[ ] Storage CA validation succeeds
[ ] captured SPKI belongs to that Storage Node
[ ] RT014 trust store contains the expected node SPKI binding
[ ] a wrong SPKI pin is rejected
[ ] Storage UI is reachable from authorized management network
[ ] Storage UI is not unintentionally public
[ ] remote support path, when configured, is customer-isolated and auditable
[ ] Director failure does not eliminate approved Storage recovery access
```

---

# 14. Current Lab Migration

The existing lab topology discovered during RT014 integration testing uses a Director-side TLS termination/proxy for the registered Storage public hostname. That topology does not satisfy this contract for authenticated cluster traffic.

Before deploying RT014 in the lab:

- provide a direct Storage HTTPS endpoint
- give Storage its own TLS identity
- make Director resolve the chosen Storage cluster hostname directly to the Storage private address
- provision the verified Storage SPKI into Director trust state
- then perform RT014/RT015 integration tests

The public-facing proxy may remain for public/user traffic if desired; it must no longer be the authenticated cluster trust endpoint.

---

# 15. Change Control

This document is an active architecture contract backed by ADR-0005.

Changes that alter the destination TLS trust boundary, remove direct Storage management/recovery access, combine unrelated customer management networks, or make public infrastructure part of authenticated cluster transport require explicit architecture/security review and a follow-up ADR.
