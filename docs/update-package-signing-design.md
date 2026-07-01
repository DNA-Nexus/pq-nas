# DNA-Nexus / PQ-NAS Signed Update Package Design

Status: draft  
Purpose: preserve UI-only updates while preventing pqnas service-user compromise from becoming arbitrary root-level binary replacement.

---

## Problem

The Update Center currently stages packages and plans under:

    /var/lib/pqnas/updates/incoming
    /var/lib/pqnas/updates/plans

These directories are writable by the pqnas service user.

The root apply wrapper then runs:

    /usr/local/sbin/pqnas-update-apply --plan-id <plan_id>

and the Python helper validates:

    plan_id
    plan_hash
    package_sha256
    target paths
    action types

This provides integrity between the saved plan and uploaded package, but it does
not prove that the package is an official DNA-Nexus / PQ-NAS release.

If an attacker gains code execution as the pqnas service user, they may be able
to create a matching package + plan pair and ask the root wrapper to apply it.

---

## Product requirement

Normal updates must remain easy for a new admin.

The intended admin flow is:

    Upload package
    Build plan
    Dry-run
    Apply

The admin must not need shell access, root commands, or manual policy files.

---

## Security goal

Before root applies a core binary update, the root helper must verify that the
update package is an official signed release.

This separates:

    integrity: package matches plan/hash
    authenticity: package was signed by DNA-Nexus / PQ-NAS release key

---

## Proposed signing model

Algorithm:

    Ed25519

Release private key:

    Stored only on trusted release machine.
    Never committed to git.
    Never shipped to customers.

Trusted public key:

    Installed on customer system as root-owned file:

        /etc/pqnas/update-trust.d/pqnas-release-2026.pub

Package contains:

    pqnas-update-manifest.v1.json
    pqnas-update-manifest.v1.sig
    payload/...

---

## Manifest requirements

The signed manifest must bind all security-sensitive install intent.

At minimum, it should contain:

    manifest_version
    product
    package_version
    created_at
    signing_key_id
    package_type
    actions[]
    files[]

Each action must bind:

    type
    action
    source
    target
    sha256
    mode

Example action:

    {
      "type": "core_binary",
      "action": "update",
      "source": "payload/usr/local/bin/pqnas_server",
      "target": "/usr/local/bin/pqnas_server",
      "sha256": "<sha256>",
      "mode": "0755"
    }

For static files:

    {
      "type": "static_file",
      "action": "update",
      "source": "payload/opt/pqnas/static/admin_updates.js",
      "target": "/opt/pqnas/static/admin_updates.js",
      "sha256": "<sha256>",
      "mode": "0644"
    }

---

## Root-helper verification

Before apply, pqnas_update_apply.py must:

    1. Safely extract the package into a temporary work directory.
    2. Locate pqnas-update-manifest.v1.json.
    3. Locate pqnas-update-manifest.v1.sig.
    4. Verify signature using trusted root-owned public key.
    5. Verify every manifest file sha256.
    6. Verify every plan action is present in the signed manifest.
    7. Verify source, target, action type, sha256, and mode match.
    8. Only then apply core_binary or static_file actions.

The server/UI may display signature status, but root-helper verification is the
enforcement point.

---

## Fail-closed behavior

For core_binary actions:

    missing manifest       => reject
    missing signature      => reject
    invalid signature      => reject
    missing trusted key    => reject
    plan/manifest mismatch => reject

For static_file actions:

    Initial migration may allow unsigned static_file updates if needed.
    Preferred production behavior is to require signatures for all update actions.

---

## Why plan_hash and package_sha256 are not enough

plan_hash and package_sha256 prove that the plan and package match each other.

They do not prove that the package came from DNA-Nexus / PQ-NAS.

Signed manifests add authenticity.

---

## Implementation locations

Release packaging:

    tools/release/make_tarball.sh

Root verification:

    server/src/updates/pqnas_update_apply.py

Trusted key installation:

    tools/installer/pqnas_install.py

Optional UI status:

    server/src/updates/update_center_routes.cpp
    server/src/static/admin_updates.js

---

## Migration plan

Phase 1:

    Add signed manifest generation to release tarball.
    Add verification helper code.
    Enforce signature for core_binary apply.
    Preserve current UI flow.

Phase 2:

    Display signature status in Update Center UI.
    Add key_id and version metadata to plan preview.

Phase 3:

    Require signatures for all update actions.
    Add key rotation support.

---

## Open questions

- Exact public key file format: PEM or raw base64.
- Whether to use OpenSSL CLI or a Python crypto dependency.
- Whether static_file updates are allowed unsigned during transition.
- How many trusted release keys are installed for key rotation.

