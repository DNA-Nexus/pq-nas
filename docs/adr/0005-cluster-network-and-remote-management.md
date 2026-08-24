# ADR-0005: Cluster Network and Remote Management

## Status

Accepted.

## Context

DNA-Nexus cluster deployments must work predictably in the home lab and in customer data centers without redesigning the network trust model for every installation.

The Director / Storage architecture already defines authenticated Director-to-Storage communication as a trusted internal cluster boundary. RT014 adds Storage-node TLS SPKI identity pinning on that boundary.

Integration testing exposed an important deployment risk: if a Storage public hostname terminates TLS on a reverse proxy, CDN, router, or Director-side proxy and then forwards plain HTTP to the Storage Node, the Director can end up pinning the proxy's TLS key rather than the Storage Node's identity. That violates the intended RT014 trust model even when the application code is correct.

Storage Nodes also need direct administrative access. During bootstrap an administrator may need to reach the Storage UI before enrollment to create the initial admin and configure the node. During recovery, administrators must still be able to reach a Storage Node even when the Director is unavailable.

Customer sites may be many hours away, so controlled remote support is an operational requirement. At the same time, exposing Storage UI or SSH directly to the public Internet is not an acceptable default.

## Decision

DNA-Nexus cluster deployments use three logically separate network planes:

1. **Public/user plane** for normal browser, mobile, sharing, and other intentionally public product traffic.
2. **Cluster/control plane** for Director-to-Storage authenticated internal communication.
3. **Management/support plane** for customer administrators and authorized vendor support.

### Cluster/control plane

Director-to-Storage authenticated control-plane traffic MUST use a direct private-network TLS endpoint owned by the destination Storage Node.

The TLS connection MUST terminate on the destination Storage Node or on a TLS endpoint that is part of that same node's trusted local boundary and uses that node's unique private key.

A public CDN, public reverse proxy, Internet hairpin path, unrelated load balancer, Director-side TLS proxy, or other third-party TLS termination MUST NOT sit between the Director and the Storage Node for authenticated cluster traffic.

The Director resolves each Storage cluster hostname directly to the Storage Node's private/cluster address through internal DNS or an equivalent controlled name mapping.

Normal Director-to-Storage authentication is layered:

```text
private network reachability
        +
TLS CA validation
        +
hostname validation
        +
RT014 exact Storage SPKI pin
        +
cluster bearer / protocol authorization
```

Each Storage Node MUST have its own TLS private key. Node private keys MUST NOT be copied between nodes or reused from a Director/public reverse proxy.

A per-customer or per-cluster private CA is the preferred default when the customer does not provide an appropriate internal PKI. Customer-managed PKI may be used when it satisfies the same identity and lifecycle requirements. A single global private CA key shared across unrelated customers is not the default architecture.

### Storage administrative access

A Storage Node MUST remain directly administrable over HTTPS from an authorized management network.

This direct Storage UI access is required for:

- initial bootstrap before cluster enrollment when needed
- creation/configuration of the initial administrator when needed
- Storage-local operational administration
- diagnostics
- recovery when the Director is unavailable

The Storage UI MUST NOT be made directly public merely to provide administration.

The same Storage HTTPS listener may serve both the administrative UI and cluster API, provided route authentication/authorization remains role-correct and network policy limits who can reach the listener.

### Remote support

Remote support MUST enter through an authenticated management path such as a customer-approved VPN, private access gateway, or bastion/jump host.

The architecture does not mandate one VPN product. The security contract is:

- no direct public Storage UI or SSH as the normal support path
- strong administrator authentication; MFA should be required for remote support
- least-privilege network reachability
- support access scoped to one customer environment
- auditability of remote administrative access
- customer policy controls whether vendor access is persistent, on-demand, or time-limited
- customer environments remain isolated from one another

A single flat vendor VPN or management network that implicitly connects unrelated customer clusters is prohibited.

Where a customer requires approval-gated support, the target operating model should allow the customer to enable vendor access for a bounded support window and have that access close automatically afterward.

### Firewall intent

The normal Storage network policy is conceptually:

```text
Director / cluster network  -> Storage HTTPS :443   allow
Admin / management network  -> Storage HTTPS :443   allow
Approved SSH management     -> Storage SSH          allow when required
Public Internet             -> Storage management   deny
```

Exact addresses, VLANs, firewall products, and VPN technologies are deployment-specific. The trust boundaries above are not.

## Standard Deployment Shape

```text
PUBLIC / USER PLANE

users and clients
       |
       v
public ingress / Director-facing services


PRIVATE CLUSTER / CONTROL PLANE

Director
   |
   | direct private HTTPS
   | CA + hostname + SPKI pin + cluster credential
   v
Storage-01 :443 ----> local pqnas_server
Storage-02 :443 ----> local pqnas_server


MANAGEMENT / SUPPORT PLANE

customer admin or authorized support
       |
       | VPN / private access / bastion + MFA
       v
management network
       |
       +----> Director administration
       +----> Storage-01 UI / approved SSH
       +----> Storage-02 UI / approved SSH
```

The home lab should intentionally follow the same logical layout as customer deployments. Address ranges may differ, but the trust and routing model should not.

## Bootstrap Contract

A new Storage Node is expected to support the following deployment flow:

1. install/configure the Storage role
2. attach it to the private cluster and management networks
3. generate the node TLS private key on the Storage Node
4. obtain/install the Storage certificate
5. make the Storage HTTPS/UI endpoint available to authorized administrators
6. perform required initial administrator/configuration steps
7. enroll the Storage Node with the Director
8. bind the Storage TLS identity in Director trust state
9. verify authenticated heartbeat/control-plane traffic
10. enable normal production operation

The private key should be generated on the node and remain there. Certificate issuance may use a CSR and the deployment's cluster/customer CA.

## Consequences

Benefits:

- RT014 pins the actual Storage Node identity rather than a proxy identity
- lab and customer deployments share one understandable model
- public ingress decisions do not silently redefine cluster trust
- Storage remains recoverable when Director is down
- remote support is possible without exposing management directly to the Internet
- customer environments can remain isolated and auditable

Tradeoffs:

- customer installations need an internal naming/routing plan
- Storage Nodes need HTTPS certificates and lifecycle management
- remote support requires an approved VPN/private-access mechanism
- some customer sites may require integration with their existing PKI, VLAN, firewall, or bastion standards

## Rejected Alternatives

### Use public Storage hostnames through arbitrary public reverse proxies for cluster traffic

Rejected because TLS pinning would authenticate the proxy termination point rather than necessarily authenticate the Storage Node.

### Expose Storage UI and SSH directly to the Internet

Rejected as the default because it unnecessarily enlarges the management attack surface.

### Copy one TLS private key to Director and all Storage Nodes

Rejected because node identity would no longer be unique and compromise of one machine could impersonate others.

### Require one specific VPN product everywhere

Rejected because customer infrastructure varies. The management security contract is fixed; the compliant implementation may differ.

### One shared vendor management network across all customers

Rejected because a compromise or routing mistake in one environment must not create a path into another customer environment.

## Current Implementation / Migration Note

RT014 implements application-level Storage SPKI pinning, but existing deployments must be checked before enabling it. A deployment whose Director reaches Storage through a Director-side or public TLS termination point is not compliant with this ADR and must migrate to a direct private Storage TLS endpoint first.

The current home lab discovered such a non-compliant proxy path during RT014 integration testing. The lab must be migrated to this standard before RT014 is deployed there.

## Change Control

This is an accepted architecture decision. Future deployments should follow it by default.

Changing the three-plane model, allowing external TLS termination in authenticated Director-to-Storage traffic, removing direct Storage recovery administration, or weakening customer separation for remote support requires an explicit follow-up ADR and security review. Small implementation choices that preserve these guarantees do not require a new ADR.
