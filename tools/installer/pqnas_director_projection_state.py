#!/usr/bin/env python3
"""Explicit Director projection sync-state provisioning."""

from __future__ import annotations

import json
import os

from pqnas_cluster_config import (
    ClusterInstallConfig,
    validate_cluster_config,
)
from pqnas_mutable_config import (
    atomic_write_managed_text,
    prepare_mutable_config_directory,
)


DIRECTOR_PROJECTION_SYNC_STATE_FILENAME = (
    "storage_projection_sync_state.json"
)
DIRECTOR_PROJECTION_SYNC_STATE_SCHEMA_VERSION = 1
DIRECTOR_PROJECTION_BOOTSTRAP_CONFIRM_SUFFIX = (
    "BOOTSTRAP DIRECTOR STATE"
)


class DirectorProjectionStateError(RuntimeError):
    pass


def director_projection_sync_state_path(root: str) -> str:
    if (
        not isinstance(root, str)
        or not root
        or "\x00" in root
        or not os.path.isabs(root)
    ):
        raise DirectorProjectionStateError(
            "Director projection state root must be an absolute path"
        )

    normalized = os.path.normpath(root)

    # Security: do not silently normalize dot-components or other alternate
    # textual path forms at this control-plane trust boundary.
    if normalized != root or normalized == "/":
        raise DirectorProjectionStateError(
            "Director projection state root must be normalized and non-root"
        )

    return os.path.join(
        normalized,
        "config",
        DIRECTOR_PROJECTION_SYNC_STATE_FILENAME,
    )


def director_projection_bootstrap_confirmation_required(
    root: str,
    install_mode: str,
    selected_config: ClusterInstallConfig,
) -> bool:
    """
    Decide whether the UI must request explicit Director-state bootstrap.

    Security: this is only a confirmation/UI decision. The execution helper
    independently refuses missing state unless the approval bit is present.
    """
    selected = validate_cluster_config(selected_config)

    if selected.role != "director":
        return False

    if install_mode == "disk":
        # A disk-mode installation replaces the filesystem mounted at the
        # PQ-NAS root, so an existing Director sync-state cannot be relied on
        # to survive the destructive operation.
        return True

    if install_mode != "existing":
        raise DirectorProjectionStateError(
            "invalid install mode for Director projection bootstrap"
        )

    return not os.path.lexists(
        director_projection_sync_state_path(root)
    )


def binary_rollback_restart_allowed(
    install_mode: str,
    projection_state_action: str,
) -> bool:
    """
    Return whether an old binary may be restarted automatically after rollback.

    Security: binary rollback does not restore data or control-plane state.
    Destructive installs and newly created Director authority therefore require
    an explicit operator recovery decision before the service may start again.
    """
    if install_mode not in ("disk", "existing"):
        raise DirectorProjectionStateError(
            "invalid install mode for rollback restart policy"
        )

    if projection_state_action not in (
        "not-applicable",
        "preserved",
        "created",
    ):
        raise DirectorProjectionStateError(
            "invalid Director projection state action"
        )

    return (
        install_mode == "existing"
        and projection_state_action != "created"
    )


def ensure_director_projection_sync_state(
    root: str,
    selected_config: ClusterInstallConfig,
    *,
    bootstrap_approved: bool,
    owner_uid: int,
    owner_gid: int,
) -> str:
    """
    Preserve or explicitly create the Director projection sync-state.

    Existing authoritative state is never replaced here. Missing state is
    created only after an explicit operator acknowledgement.
    """
    selected = validate_cluster_config(selected_config)

    if selected.role != "director":
        return "not-applicable"

    config_dir = prepare_mutable_config_directory(
        root,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )
    target = os.path.join(
        config_dir,
        DIRECTOR_PROJECTION_SYNC_STATE_FILENAME,
    )

    # Security: never rewrite, reset, migrate, or reinterpret an existing
    # authoritative confirmed-sync snapshot from the bootstrap path. Runtime
    # performs the strict schema/ownership/symlink validation before use.
    if os.path.lexists(target):
        return "preserved"

    if not bootstrap_approved:
        raise DirectorProjectionStateError(
            "Director projection sync-state is missing; "
            "explicit Director bootstrap approval is required"
        )

    contents = json.dumps(
        {
            "version":
                DIRECTOR_PROJECTION_SYNC_STATE_SCHEMA_VERSION,
            "states": [],
        },
        indent=2,
    ) + "\n"

    # Security: create-only atomic persistence prevents bootstrap from
    # overwriting a state file that appears concurrently. Mode 0600 matches
    # the runtime requirement that no group/world permission bits are set.
    created = atomic_write_managed_text(
        target,
        contents,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
        file_mode=0o600,
        replace_existing=False,
    )

    return "created" if created else "preserved"
