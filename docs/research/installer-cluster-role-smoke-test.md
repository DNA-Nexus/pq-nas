# Installer cluster-role end-to-end smoke test

## Purpose

This runbook verifies the installer and runtime behavior for Standalone,
Director, and Storage node roles on disposable test machines. It is a manual
end-to-end check, not a replacement for the existing unit and CMake regression
tests.

Known-good starting point:

- repository: `DNA-Nexus/dna-nexus-cluster`
- current installer role implementation: `c02f8997`
- pre-role upgrade fixture: `415fd838`
- installer role PR: `#6`
- restart and per-node heartbeat PR: `#5`

## Safety boundary

Run these tests only on dedicated virtual machines or physical test machines.

Do not use:

- a production server
- a production Director
- production storage disks
- existing customer data
- production enrollment tokens
- production certificates or private keys
- a host-mounted data directory
- a shared block device
- a network that can reach production cluster control-plane endpoints

Each destructive installation needs a separate disposable data disk. Verify the
disk identity from both the hypervisor and the guest before accepting the
installer's `WIPE` confirmation.

Take a powered-off snapshot before each destructive or adversarial test.

## Suggested lab topology

| Machine | Purpose |
|---|---|
| `dn-lab-standalone` | fresh Standalone and Standalone upgrade |
| `dn-lab-director` | Director and enrollment control plane |
| `dn-lab-node01` | Storage node lifecycle and adversarial filesystem cases |

Use an isolated lab network. Internet access may be provided through NAT for
package installation, but the lab network must not route to production nodes.

Director and Storage node URLs must use HTTPS. Do not disable certificate
verification to make the smoke test pass. Use the same trust or SPKI pinning
model intended for deployment.

## Evidence handling

The evidence collector is read-only:

```bash
sudo python3 \
  tools/dev/collect_installer_cluster_smoke_evidence.py \
  --phase standalone
```

Create private evidence files:

```bash
umask 077

sudo python3 \
  tools/dev/collect_installer_cluster_smoke_evidence.py \
  --phase standalone \
  > standalone-evidence.json
```

The collector:

- reports metadata, not file contents
- opens `pqnas.env` through no-follow descriptors and validates the
  root-owned `0600` inode before reading it
- does not read `keys.env`
- exposes only environment key names and validated non-secret cluster settings
- follows validated configured node credential/token paths instead of
  assuming only the default paths
- does not print credentials, tokens, raw journal output or raw process data
- wipes mutable raw env, journal, process and token buffers after checks
- uses stable reason codes instead of raw exceptions

For an exact bootstrap-token leak scan, enter the already issued lab token
without putting it in argv or an exported environment variable:

```bash
read -rsp "Lab bootstrap token: " PQNAS_SMOKE_TOKEN
printf '\n'

printf '%s\n' "$PQNAS_SMOKE_TOKEN" |
  sudo python3 \
    tools/dev/collect_installer_cluster_smoke_evidence.py \
    --phase node-enrolled \
    --secret-stdin \
    > node-enrolled-evidence.json

unset PQNAS_SMOKE_TOKEN
```

A `warn` result from a journal or process scan means the evidence is
incomplete; it must not be treated as proof that the token is absent.

Do not paste the token into an evidence file, issue, PR, terminal transcript or
chat. Delete shell history entries if the token was previously assigned on a
command line.

## Common preflight

On every machine:

```bash
git status --short --branch
git rev-parse --short=8 HEAD

lsblk -o NAME,PATH,SIZE,TYPE,FSTYPE,MOUNTPOINTS,MODEL,SERIAL
findmnt
ip -brief address
```

Before installer execution:

1. Confirm the expected commit.
2. Confirm the selected data disk is disposable.
3. Confirm the system disk is not selected.
4. Confirm no production mount is present.
5. Confirm the machine hostname and lab IP.
6. Take or verify the VM snapshot.
7. Record the expected server role and URLs.
8. Do not record plaintext tokens.

Start the installer from the checked-out source tree:

```bash
sudo python3 tools/installer/pqnas_install.py
```

Use the wizard's exact typed confirmation. Do not automate or bypass the
destructive confirmation.

## Shared post-install checks

After each successful installation:

```bash
sudo systemctl is-active pqnas.service
sudo systemctl status pqnas.service --no-pager -l
sudo systemctl show pqnas.service \
  -p User \
  -p Group \
  -p ProtectSystem \
  -p ReadWritePaths \
  -p EnvironmentFiles \
  --no-pager

sudo stat -c '%n type=%F uid=%u gid=%g mode=%a links=%h' \
  /etc/pqnas \
  /etc/pqnas/pqnas.env \
  /etc/pqnas/keys.env \
  /srv/pqnas \
  /srv/pqnas/config \
  /srv/pqnas/data \
  /var/lib/pqnas
```

Never run `cat`, `grep`, `sed`, `env`, `systemctl show --all`, or similar
commands against secret-bearing files merely to collect evidence.

## Test S01: fresh Standalone

Starting state:

- clean VM snapshot
- no previous `/etc/pqnas`, `/srv/pqnas`, `/var/lib/pqnas`
- disposable data disk

Installer choices:

- fresh installation
- intended storage backend
- role `Standalone`

Expected:

- service starts
- `PQNAS_SERVER_ROLE=standalone`
- no node settings are rendered
- node bootstrap token is absent
- `/etc/pqnas` is root-managed
- `pqnas.env` and `keys.env` are `root:root 0600`
- mutable runtime directories are `pqnas:pqnas 0750`
- `/etc/pqnas` is absent from `ReadWritePaths`

Evidence:

```bash
sudo python3 \
  tools/dev/collect_installer_cluster_smoke_evidence.py \
  --phase standalone
```

## Test S02: upgrade an old Standalone installation

1. Restore a clean snapshot.
2. Install the Standalone fixture from commit `415fd838`.
3. Confirm the service and existing test data work.
4. Record non-secret test-data filenames and checksums.
5. Update the source tree to `c02f8997`.
6. Run the installer as an upgrade and keep role `Standalone`.
7. Confirm test data and validated configuration survive.

Expected:

- no disk formatting during upgrade
- role remains Standalone
- existing mutable data survives
- root-managed environment/key files remain protected
- service starts using the new binary

## Test S03: fresh Director

Starting state:

- clean Director VM snapshot
- no Storage node configuration

Installer choice:

- role `Director`

Expected:

- `PQNAS_SERVER_ROLE=director`
- no node ID, Director URL, node credential path or bootstrap-token path
- obsolete global Director credential setting is rejected
- service starts
- enrollment administration is available only through authorized admin flow

Evidence:

```bash
sudo python3 \
  tools/dev/collect_installer_cluster_smoke_evidence.py \
  --phase director
```

## Test S04: fresh Storage node with a valid token

On the Director:

1. Create a new enrollment for `cellnode1` through the existing authorized
   cluster-enrollment administration flow.
2. Record the non-secret enrollment ID and generation.
3. Transfer the one-time token only through the secure lab procedure.
4. Do not save the plaintext token in notes or evidence.

On the node installer, provide:

- node ID
- Director HTTPS URL
- enrollment ID
- enrollment generation
- node HTTPS API URL
- node credential file
- bootstrap-token file
- canonical one-time bootstrap token

Before first service enrollment, collect:

```bash
sudo python3 \
  tools/dev/collect_installer_cluster_smoke_evidence.py \
  --phase node-before-enrollment
```

Expected after enrollment:

- Director reports enrollment consumed/active
- node credential exists as `pqnas:pqnas 0600`
- bootstrap-token file is removed
- heartbeat reaches the Director
- no credential or bearer value appears in node status
- service remains active

Then run the exact-token leak scan described above.

## Test S05: restart after token consumption

After S04:

```bash
sudo systemctl restart pqnas.service
sudo systemctl is-active pqnas.service
```

Expected:

- restart succeeds without recreating the bootstrap token
- exact persisted enrollment intent matches
- existing node credential is used
- Director receives a new heartbeat
- token remains absent

## Test S06: unchanged Storage node upgrade without a new token

1. Keep the exact node ID, Director URL, enrollment ID, generation, API URL,
   credential path and bootstrap-token path.
2. Run the installer as an upgrade.
3. Leave the bootstrap-token input empty only when the wizard permits the
   unchanged persisted configuration.

Expected:

- installer preserves the validated node configuration
- no new token is required
- existing credential is preserved
- token remains absent
- service starts and heartbeat resumes

## Test S07: changed node configuration requires a new token

Repeat separately for at least:

- changed node ID
- changed Director URL
- changed enrollment ID
- changed enrollment generation
- changed node API URL
- changed credential path
- changed bootstrap-token path

After each accepted configuration variant, run the collector so its node
file checks follow the validated configured credential and token paths rather
than only the defaults.

Expected:

- changed node configuration without a token fails closed
- no partial environment update is left active
- no credential is overwritten
- no old token is silently reused
- service is not started with an inconsistent configuration

Restore the snapshot between variants.

## Test S08: node to Standalone or Director

Run both transitions from a snapshot containing an unconsumed token:

- node → Standalone
- node → Director

Expected:

- old bootstrap-token file is safely removed
- selected non-node role is rendered without node settings
- no token value enters `pqnas.env`
- service starts in the selected role

The old credential may remain as inert state unless a separate approved cleanup
policy says otherwise. Do not delete credentials manually during this test.

## Test S09: invalid bootstrap tokens

Test separately:

- empty value when a new/changed enrollment requires a token
- 42 characters
- 44 characters
- `=` padding
- leading or trailing whitespace
- `+` or `/`
- non-ASCII
- canonical-length value with non-zero base64 padding bits

Expected:

- installer rejects the value before writing a token file
- no plaintext value appears in logs
- no partial node configuration is activated
- existing safe state is preserved

## Test S10: unsafe filesystem metadata

Use a fresh snapshot for every case. Apply the unsafe state only inside the
disposable VM.

Cases:

- token target is a symlink
- token target has an additional hardlink
- token owner is wrong
- token group is wrong
- token mode is not `0600`
- bootstrap parent is a symlink
- bootstrap parent is group/world writable
- credential target is a symlink
- credential target has unsafe owner, mode or link count

Expected:

- installer/runtime fails closed
- external symlink targets are not modified
- hardlinked content is not modified
- no unsafe path is replaced or removed
- no raw exception or secret is logged
- failure uses a stable reason/error code

A deterministic TOCTOU test requires a dedicated test hook or harness. Do not
try to win a race manually on a live machine.

## Director and heartbeat evidence

Verify through the existing authorized Director UI/API:

- enrollment ID and generation match
- enrollment becomes consumed/active
- node ID and API URL match
- heartbeat timestamp advances
- restart produces a later heartbeat
- disabling one node does not affect other nodes
- a missing credential affects only its own node

Do not include bearer tokens, credentials, authorization headers, session
cookies or raw API responses in evidence.

## Pass criteria

The smoke-test set passes only when:

- all expected installations and upgrades start successfully
- every intentionally unsafe case fails closed
- token file lifecycle matches the selected phase
- node credential metadata is correct
- tokenless restart works only for the exact persisted enrollment intent
- changed enrollment intent requires a new token
- Director receives heartbeats
- no exact token is found in `pqnas.env`, recent service journal, process argv or
  process environments
- no production system or data was touched

## Failure handling

When a failure is found:

1. Stop that scenario.
2. Preserve only redacted evidence.
3. Restore the VM snapshot.
4. Open a separate narrowly scoped feature branch.
5. Add a focused regression test.
6. Fix one defect class.
7. Run installer tests and relevant CMake regressions.
8. Build `pqnas_server` when runtime code changes.
9. Send the fix through a separate PR and Semgrep gate.

Do not mix unrelated runtime fixes into the smoke-test runbook branch.
