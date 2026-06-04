# PQ-NAS Update Manifest

This document explains `tools/release/update_manifest.template.json`.

The release tarball builder generates the final manifest into the tarball at:

```text
pqnas/update_manifest.json
```

The source template lives at:

```text
tools/release/update_manifest.template.json
```

The manifest is package metadata for DNA-Nexus / PQ-NAS Update Center. Its purpose is to make update packages self-describing, so Update Center can decide whether a package is safe to install from the currently installed version.

It is especially important for future structural updates where a release changes more than static files or the server binary, for example:

- new environment defaults
- new storage paths
- new systemd units
- new database or metadata migrations
- new Update Center capabilities
- mandatory intermediate update steps

## Current updater scope

At the time this manifest format was introduced, the in-place updater safely supports mainly:

```text
static_file
core_binary
```

That means normal updates can replace static UI files and the main server binary.

More structural actions such as environment changes, directory creation, migrations, or systemd changes should not be silently applied until the updater explicitly supports them.

Example update path:

```text
1.1.7 -> 1.1.8
1.1.8 upgrades Update Center to support env_default, ensure_directory, and migration actions.
1.1.9 may then require 1.1.8 before installation.
```

## Template placeholders

### `__PQNAS_VERSION__`

The compiled server version from:

```text
server/src/version.h
```

Example:

```json
"package_version": "__PQNAS_VERSION__"
```

becomes:

```json
"package_version": "1.1.8"
```

### `__PQNAS_TARBALL_VERSION__`

The version argument passed to:

```bash
tools/release/make_tarball.sh 1.1.8
```

This should match `PQNAS_VERSION`.

### `__PQNAS_ARCH__`

The release architecture, currently usually:

```text
x86_64
```

## Field reference

### `schema`

```json
"schema": 1
```

The manifest format version. If the structure changes later in a breaking way, this can become `2`. Old Update Center versions should reject unknown schema versions instead of guessing.

### `kind`

```json
"kind": "pqnas_update_manifest"
```

Identifies the file as a PQ-NAS update manifest. This prevents an unrelated JSON file from being treated as an update manifest.

### `package`

```json
"package": "pqnas"
```

The product/package family. Currently this should be `pqnas`.

### `package_version`

```json
"package_version": "__PQNAS_VERSION__"
```

The version of the server contained in this package. After release generation:

```json
"package_version": "1.1.8"
```

This should match the compiled version in `server/src/version.h`.

### `update_id`

```json
"update_id": "pqnas-__PQNAS_VERSION__"
```

A stable identifier for this update.

Example:

```json
"update_id": "pqnas-1.1.8"
```

This can later be used for audit logs, migration tracking, and already-applied checks.

### `artifact`

```json
{
  "name": "pqnas-__PQNAS_TARBALL_VERSION__-linux-__PQNAS_ARCH__.tar.gz",
  "arch": "__PQNAS_ARCH__",
  "format": "tar.gz"
}
```

Generated example:

```json
{
  "name": "pqnas-1.1.8-linux-x86_64.tar.gz",
  "arch": "x86_64",
  "format": "tar.gz"
}
```

Possible future checks:

- reject wrong architecture
- reject unsupported format
- warn if artifact name and package version do not match

## Compatibility rules

The `compatibility` block is the most important part for update ordering.

```json
{
  "min_current_version": "1.1.7",
  "max_current_version_exclusive": "__PQNAS_VERSION__",
  "requires_prior": []
}
```

### `min_current_version`

The oldest installed version that may install this package.

Example:

```json
"min_current_version": "1.1.7"
```

This means: current version must be 1.1.7 or newer.

### `max_current_version_exclusive`

The installed version must be lower than this value. Usually this should stay:

```json
"max_current_version_exclusive": "__PQNAS_VERSION__"
```

For a `1.1.8` package it becomes:

```json
"max_current_version_exclusive": "1.1.8"
```

This prevents reinstalling the same version or downgrading from a newer version.

### `requires_prior`

Human-readable instructions for required intermediate versions.

Example:

```json
{
  "version": "1.1.8",
  "message": "Install 1.1.8 first. It upgrades Update Center to support env_default, ensure_directory, and migration actions."
}
```

## Common compatibility examples

### Example 1: first manifest-aware package

If `1.1.7` is the first version that supports manifest-aware update checks, and you are building `1.1.8`, use:

```json
{
  "min_current_version": "1.1.7",
  "max_current_version_exclusive": "__PQNAS_VERSION__",
  "requires_prior": []
}
```

Generated in the `1.1.8` tarball:

```json
{
  "min_current_version": "1.1.7",
  "max_current_version_exclusive": "1.1.8",
  "requires_prior": []
}
```

Allowed:

```text
1.1.7 -> 1.1.8
```

Rejected:

```text
1.1.6 -> 1.1.8
1.1.8 -> 1.1.8
1.1.9 -> 1.1.8
```

### Example 2: normal next update

If `1.1.8` is installed and `1.1.9` is a normal update:

```json
{
  "min_current_version": "1.1.8",
  "max_current_version_exclusive": "__PQNAS_VERSION__",
  "requires_prior": []
}
```

### Example 3: force an intermediate structural update

Suppose `1.1.8` upgrades Update Center so it can safely handle future manifest actions such as `env_default`, `ensure_directory`, `migration`, and `systemd_unit`. Then `1.1.9` should require `1.1.8`:

```json
{
  "min_current_version": "1.1.8",
  "max_current_version_exclusive": "__PQNAS_VERSION__",
  "requires_prior": [
    {
      "version": "1.1.8",
      "message": "Install 1.1.8 first. It upgrades Update Center to support env_default, ensure_directory, and migration actions."
    }
  ]
}
```

If an admin tries `1.1.7 -> 1.1.9`, Update Center should reject the plan and display the `requires_prior` message.

## Capabilities

The `capabilities` block describes updater features.

```json
{
  "requires": [],
  "provides": [
    "update-manifest-v1"
  ]
}
```

### `requires`

Capabilities that the currently installed Update Center must already support before this package can be installed.

Current basic packages usually use:

```json
"requires": []
```

Future structural packages may use:

```json
"requires": [
  "update-manifest-v1",
  "env-default-v1",
  "ensure-directory-v1",
  "migration-v1"
]
```

If the current Update Center does not support one of these, it should reject the package and tell the admin to install an intermediate update first.

### `provides`

Capabilities added by this update.

Example for a normal manifest-aware release:

```json
"provides": [
  "update-manifest-v1"
]
```

Example for a future structural updater release:

```json
"provides": [
  "update-manifest-v1",
  "env-default-v1",
  "ensure-directory-v1",
  "migration-v1"
]
```

## Actions policy

The `actions_policy` block documents what action types are safe now and what action types are planned for future manifest-driven updates.

```json
{
  "safe_in_place_actions": [
    "static_file",
    "core_binary"
  ],
  "future_manifest_actions": [
    "ensure_directory",
    "env_default",
    "migration",
    "systemd_unit"
  ]
}
```

### `safe_in_place_actions`

Action types that the current updater can apply safely. Currently: `static_file` and `core_binary`.

### `future_manifest_actions`

Action types planned for future updater support. These are not automatically safe just because they are listed here. They become safe only after Update Center explicitly implements them.

Possible future examples:

```json
{
  "type": "ensure_directory",
  "path": "/var/lib/pqnas/circle_stack/federation",
  "owner": "pqnas",
  "group": "pqnas",
  "mode": "0750"
}
```

```json
{
  "type": "env_default",
  "file": "/etc/pqnas/pqnas.env",
  "key": "PQNAS_CIRCLE_FEDERATION_ENABLED",
  "value": "0",
  "mode": "add_if_missing"
}
```

```json
{
  "type": "migration",
  "id": "2026-06-03-circle-federation-schema",
  "description": "Create Circle Stack federation metadata tables",
  "apply": "migrations/2026-06-03-circle-federation-schema.py",
  "idempotent": true
}
```

## Release checklist

1. Update the compiled server version in `server/src/version.h`.

```c
#define PQNAS_VERSION "1.1.8"
```

2. Edit compatibility in `tools/release/update_manifest.template.json`.

Example if `1.1.7` is the minimum supported base:

```json
{
  "min_current_version": "1.1.7",
  "max_current_version_exclusive": "__PQNAS_VERSION__",
  "requires_prior": []
}
```

3. Build the tarball.

```bash
tools/release/make_tarball.sh 1.1.8
```

4. Confirm the manifest is inside the tarball.

```bash
tar -tzf /tmp/pqnas-release/pqnas-1.1.8-linux-x86_64.tar.gz | grep update_manifest
```

Expected:

```text
pqnas/update_manifest.json
```

5. Inspect the generated manifest.

```bash
rm -rf /tmp/pqnas-test
mkdir -p /tmp/pqnas-test
tar -xzf /tmp/pqnas-release/pqnas-1.1.8-linux-x86_64.tar.gz -C /tmp/pqnas-test
python3 -m json.tool /tmp/pqnas-test/pqnas/update_manifest.json
```

6. Check especially:

- `package_version`
- `update_id`
- `artifact.name`
- `compatibility.min_current_version`
- `compatibility.max_current_version_exclusive`
- `capabilities.requires`
- `capabilities.provides`

## Rule of thumb

For normal small updates:

```json
"min_current_version": "previous_release_version"
```

For a structural update that upgrades updater capabilities:

```json
"provides": [
  "update-manifest-v1",
  "env-default-v1",
  "ensure-directory-v1",
  "migration-v1"
]
```

For the release after that structural update:

```json
"requires": [
  "env-default-v1",
  "ensure-directory-v1",
  "migration-v1"
]
```

and:

```json
"min_current_version": "the_structural_update_version"
```

This forces admins to install the structural updater release first.
