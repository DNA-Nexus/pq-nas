# ADR-0001: Documentation Structure

## Status

Accepted.

## Context

DNA-Nexus Server has grown from an early PQ-NAS prototype into a larger product with authentication, storage, sharing, workspaces, bundled apps, Drop Zone, Circle Stack, Echo Stack, Reel Stack, audit logging, storage tooling, and federation research.

The top-level `README.md` already explains the public product story and main features.

As the project grows, important knowledge should not live only in chat history, handwritten notes, or source code. The project needs a documentation structure that can hold product requirements, architecture, technical design, security notes, operations guidance, research checkpoints, and user/admin guides.

## Decision

Create a structured `docs/` directory with focused subdirectories:

```text
docs/
├─ product/
├─ architecture/
├─ technical/
├─ security/
├─ adr/
├─ user-guides/
├─ operations/
└─ research/
```

The top-level `docs/README.md` acts as the documentation map.

The repository root `README.md` remains the public overview.

## Consequences

Benefits:

- easier onboarding
- less knowledge trapped in memory or chat history
- clearer product planning
- better technical decision history
- easier security reviews
- easier operator/customer-facing documentation later

Tradeoffs:

- documentation must be maintained
- stale documents can become misleading
- overly large documents should be avoided

## Rules

Documentation should stay practical.

Prefer:

- small focused documents
- clear requirements
- explicit non-goals
- architecture decisions with reasons
- checklists for fragile areas
- references to existing implementation where useful

Avoid:

- duplicating the root README
- writing huge documents nobody maintains
- documenting imaginary behavior as if it already exists
- hiding important security assumptions only in code comments
