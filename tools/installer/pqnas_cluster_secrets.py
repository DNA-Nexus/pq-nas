#!/usr/bin/env python3
# Security-sensitive one-time storage-node bootstrap secret handling.

from __future__ import annotations

import base64
import os
import re
import secrets
import stat
from pathlib import Path
from typing import Optional

from pqnas_cluster_config import (
    ClusterInstallConfig,
    validate_cluster_config,
)
from pqnas_mutable_config import (
    prepare_service_state_directory,
    service_owner_ids,
)


DEFAULT_NODE_CREDENTIAL_FILE = (
    "/var/lib/pqnas/cluster/node-credential"
)
DEFAULT_NODE_BOOTSTRAP_TOKEN_FILE = (
    "/var/lib/pqnas/cluster/bootstrap/enrollment.token"
)

MAX_PATH_BYTES = 4096
NODE_BOOTSTRAP_TOKEN_CHARS = 43
NODE_BOOTSTRAP_TOKEN_BYTES = 32
NODE_BOOTSTRAP_TOKEN_FILE_BYTES = 44

_TOKEN_RE = re.compile(r"[A-Za-z0-9_-]{43}\Z")


class ClusterSecretError(RuntimeError):
    pass


def wipe_secret_buffer(
    value: Optional[bytearray],
) -> None:
    if value is None:
        return

    for index in range(len(value)):
        value[index] = 0


def validate_node_bootstrap_token(
    raw: str,
) -> bytearray:
    if (
        not isinstance(raw, str)
        or len(raw) != NODE_BOOTSTRAP_TOKEN_CHARS
        or len(raw.encode("utf-8"))
            != NODE_BOOTSTRAP_TOKEN_CHARS
        or not _TOKEN_RE.fullmatch(raw)
    ):
        raise ClusterSecretError(
            "node bootstrap token has invalid format"
        )

    try:
        decoded = base64.urlsafe_b64decode(
            raw + "="
        )
    except Exception as exc:
        raise ClusterSecretError(
            "node bootstrap token has invalid encoding"
        ) from exc

    canonical = base64.urlsafe_b64encode(
        decoded
    ).decode("ascii").rstrip("=")

    if (
        len(decoded) != NODE_BOOTSTRAP_TOKEN_BYTES
        or canonical != raw
    ):
        raise ClusterSecretError(
            "node bootstrap token is not canonical"
        )

    return bytearray(
        raw.encode("ascii")
    )


def _normalized_target(
    path: str,
) -> tuple[str, str]:
    if (
        not isinstance(path, str)
        or not path
        or "\x00" in path
        or len(path.encode("utf-8")) > MAX_PATH_BYTES
        or not os.path.isabs(path)
    ):
        raise ClusterSecretError(
            "bootstrap token path must be a bounded absolute path"
        )

    normalized = os.path.normpath(path)
    target = Path(normalized)

    if (
        normalized != path
        or normalized == "/"
        or not target.name
    ):
        raise ClusterSecretError(
            "bootstrap token path must name a normalized file"
        )

    for component in target.parts:
        if component in (".", ".."):
            raise ClusterSecretError(
                "bootstrap token path contains a dot component"
            )

    return str(target.parent), target.name


def _directory_flags() -> int:
    return (
        os.O_RDONLY
        | os.O_CLOEXEC
        | os.O_DIRECTORY
        | getattr(os, "O_NOFOLLOW", 0)
    )


def _open_directory_path(
    path: str,
) -> int:
    if (
        not isinstance(path, str)
        or not path
        or not os.path.isabs(path)
    ):
        raise ClusterSecretError(
            "bootstrap token directory path is invalid"
        )

    normalized = os.path.normpath(path)
    if (
        normalized != path
        or normalized == "/"
    ):
        raise ClusterSecretError(
            "bootstrap token directory path is unsafe"
        )

    try:
        descriptor = os.open(
            "/",
            _directory_flags(),
        )
    except OSError as exc:
        raise ClusterSecretError(
            "failed to open filesystem root safely"
        ) from exc

    try:
        for component in Path(normalized).parts[1:]:
            try:
                next_descriptor = os.open(
                    component,
                    _directory_flags(),
                    dir_fd=descriptor,
                )
            except OSError as exc:
                raise ClusterSecretError(
                    "failed to open bootstrap token directory safely"
                ) from exc

            os.close(descriptor)
            descriptor = next_descriptor

        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def _open_private_parent(
    parent: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> int:
    descriptor = _open_directory_path(
        parent
    )

    try:
        metadata = os.fstat(
            descriptor
        )

        if (
            not stat.S_ISDIR(metadata.st_mode)
            or metadata.st_uid != owner_uid
            or metadata.st_gid != owner_gid
            or stat.S_IMODE(metadata.st_mode) & 0o022
        ):
            raise ClusterSecretError(
                "bootstrap token directory has unsafe metadata"
            )

        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def _validate_token_file(
    metadata: os.stat_result,
    *,
    owner_uid: int,
    owner_gid: int,
) -> None:
    if (
        not stat.S_ISREG(metadata.st_mode)
        or metadata.st_nlink != 1
        or metadata.st_uid != owner_uid
        or metadata.st_gid != owner_gid
        or stat.S_IMODE(metadata.st_mode) != 0o600
        or metadata.st_size < 0
        or metadata.st_size >
            NODE_BOOTSTRAP_TOKEN_FILE_BYTES
    ):
        raise ClusterSecretError(
            "bootstrap token file has unsafe metadata"
        )


def _inspect_target(
    directory_fd: int,
    filename: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> Optional[os.stat_result]:
    try:
        metadata = os.stat(
            filename,
            dir_fd=directory_fd,
            follow_symlinks=False,
        )
    except FileNotFoundError:
        return None
    except OSError as exc:
        raise ClusterSecretError(
            "failed to inspect bootstrap token file"
        ) from exc

    _validate_token_file(
        metadata,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )
    return metadata


def _valid_token_buffer(
    value: bytearray,
) -> bool:
    if (
        not isinstance(value, bytearray)
        or len(value) != NODE_BOOTSTRAP_TOKEN_CHARS
        or not all(
            (
                ord("A") <= byte <= ord("Z")
                or ord("a") <= byte <= ord("z")
                or ord("0") <= byte <= ord("9")
                or byte in (ord("-"), ord("_"))
            )
            for byte in value
        )
    ):
        return False

    try:
        decoded = base64.urlsafe_b64decode(
            value + b"="
        )
        canonical = bytearray(
            base64.urlsafe_b64encode(
                decoded
            ).rstrip(b"=")
        )
    except Exception:
        return False

    return (
        len(decoded) == NODE_BOOTSTRAP_TOKEN_BYTES
        and canonical == value
    )


def _write_all(
    descriptor: int,
    contents: memoryview,
) -> None:
    offset = 0

    while offset < len(contents):
        try:
            written = os.write(
                descriptor,
                contents[offset:],
            )
        except InterruptedError:
            continue
        except OSError as exc:
            raise ClusterSecretError(
                "failed to write bootstrap token file"
            ) from exc

        if written <= 0:
            raise ClusterSecretError(
                "short write while writing bootstrap token file"
            )

        offset += written


def atomic_write_node_bootstrap_token(
    path: str,
    token: bytearray,
    *,
    owner_uid: int,
    owner_gid: int,
) -> None:
    if not _valid_token_buffer(token):
        raise ClusterSecretError(
            "bootstrap token buffer has invalid format"
        )

    parent, filename = _normalized_target(
        path
    )
    directory_fd = _open_private_parent(
        parent,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )

    temporary_name = ""
    temporary_created = False
    temporary_fd = -1

    try:
        initial = _inspect_target(
            directory_fd,
            filename,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        flags = (
            os.O_WRONLY
            | os.O_CREAT
            | os.O_EXCL
            | os.O_CLOEXEC
            | getattr(os, "O_NOFOLLOW", 0)
        )

        for _ in range(16):
            candidate = (
                f".{filename}.tmp."
                f"{os.getpid()}."
                f"{secrets.token_hex(8)}"
            )

            try:
                temporary_fd = os.open(
                    candidate,
                    flags,
                    0o600,
                    dir_fd=directory_fd,
                )
                temporary_name = candidate
                temporary_created = True
                break
            except FileExistsError:
                continue
            except OSError as exc:
                raise ClusterSecretError(
                    "failed to create bootstrap token temp file"
                ) from exc

        if temporary_fd < 0:
            raise ClusterSecretError(
                "failed to reserve bootstrap token temp file"
            )

        # Security: establish final ownership and permissions on the opened
        # inode before any secret bytes are written.
        os.fchown(
            temporary_fd,
            owner_uid,
            owner_gid,
        )
        os.fchmod(
            temporary_fd,
            0o600,
        )

        token_view = memoryview(token)
        try:
            _write_all(
                temporary_fd,
                token_view,
            )
        finally:
            token_view.release()

        _write_all(
            temporary_fd,
            memoryview(b"\n"),
        )
        os.fsync(
            temporary_fd
        )

        written = os.fstat(
            temporary_fd
        )
        _validate_token_file(
            written,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        if (
            written.st_size !=
                NODE_BOOTSTRAP_TOKEN_FILE_BYTES
        ):
            raise ClusterSecretError(
                "bootstrap token temp file failed validation"
            )

        os.close(
            temporary_fd
        )
        temporary_fd = -1

        current = _inspect_target(
            directory_fd,
            filename,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        if initial is None:
            if current is not None:
                raise ClusterSecretError(
                    "bootstrap token file changed during creation"
                )
        elif (
            current is None
            or current.st_dev != initial.st_dev
            or current.st_ino != initial.st_ino
        ):
            raise ClusterSecretError(
                "bootstrap token file changed during replacement"
            )

        try:
            os.replace(
                temporary_name,
                filename,
                src_dir_fd=directory_fd,
                dst_dir_fd=directory_fd,
            )
        except OSError as exc:
            raise ClusterSecretError(
                "failed to atomically install bootstrap token"
            ) from exc

        temporary_created = False
        os.fsync(
            directory_fd
        )
    finally:
        if temporary_fd >= 0:
            os.close(
                temporary_fd
            )

        if temporary_created and temporary_name:
            try:
                os.unlink(
                    temporary_name,
                    dir_fd=directory_fd,
                )
            except OSError:
                pass

        os.close(
            directory_fd
        )


def remove_node_bootstrap_token(
    path: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> bool:
    parent, filename = _normalized_target(
        path
    )

    try:
        directory_fd = _open_private_parent(
            parent,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )
    except ClusterSecretError as exc:
        if isinstance(
            exc.__cause__,
            FileNotFoundError,
        ):
            return False
        raise

    file_fd = -1

    try:
        flags = (
            os.O_RDONLY
            | os.O_CLOEXEC
            | getattr(os, "O_NOFOLLOW", 0)
        )

        try:
            file_fd = os.open(
                filename,
                flags,
                dir_fd=directory_fd,
            )
        except FileNotFoundError:
            return False
        except OSError as exc:
            raise ClusterSecretError(
                "failed to open bootstrap token for removal"
            ) from exc

        # Security: retain the opened inode until unlink. This prevents inode
        # reuse from making a replacement look identical to the file inspected
        # for removal.
        opened = os.fstat(
            file_fd
        )
        _validate_token_file(
            opened,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        current = _inspect_target(
            directory_fd,
            filename,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        if (
            current is None
            or current.st_dev != opened.st_dev
            or current.st_ino != opened.st_ino
        ):
            raise ClusterSecretError(
                "bootstrap token file changed during removal"
            )

        try:
            os.unlink(
                filename,
                dir_fd=directory_fd,
            )
        except OSError as exc:
            raise ClusterSecretError(
                "failed to remove bootstrap token file"
            ) from exc

        os.fsync(
            directory_fd
        )
        return True
    finally:
        if file_fd >= 0:
            os.close(
                file_fd
            )
        os.close(
            directory_fd
        )


def apply_cluster_bootstrap_secret(
    previous_config: ClusterInstallConfig,
    selected_config: ClusterInstallConfig,
    token: Optional[bytearray],
) -> str:
    previous = validate_cluster_config(
        previous_config
    )
    selected = validate_cluster_config(
        selected_config
    )

    owner_uid, owner_gid = service_owner_ids(
        "pqnas"
    )

    try:
        if selected.role == "node":
            if (
                selected.cluster_credential_file ==
                    DEFAULT_NODE_CREDENTIAL_FILE
                and selected.bootstrap_token_file ==
                    DEFAULT_NODE_BOOTSTRAP_TOKEN_FILE
            ):
                # Security: enrollment credentials and bootstrap secrets
                # must not be accessible through group or other permissions.
                prepare_service_state_directory(
                    "/var/lib/pqnas/cluster",
                    owner_uid=owner_uid,
                    owner_gid=owner_gid,
                    directory_mode=0o700,
                )
                prepare_service_state_directory(
                    "/var/lib/pqnas/cluster/bootstrap",
                    owner_uid=owner_uid,
                    owner_gid=owner_gid,
                    directory_mode=0o700,
                )

            if (
                selected != previous
                and token is None
            ):
                raise ClusterSecretError(
                    "changed node enrollment requires a bootstrap token"
                )

            action = "preserved"

            if token is not None:
                atomic_write_node_bootstrap_token(
                    selected.bootstrap_token_file,
                    token,
                    owner_uid=owner_uid,
                    owner_gid=owner_gid,
                )
                action = "written"

            if (
                previous.role == "node"
                and previous.bootstrap_token_file !=
                    selected.bootstrap_token_file
            ):
                remove_node_bootstrap_token(
                    previous.bootstrap_token_file,
                    owner_uid=owner_uid,
                    owner_gid=owner_gid,
                )

            return action

        if previous.role == "node":
            removed = remove_node_bootstrap_token(
                previous.bootstrap_token_file,
                owner_uid=owner_uid,
                owner_gid=owner_gid,
            )
            return (
                "removed"
                if removed
                else "not_present"
            )

        return "not_applicable"
    finally:
        # Security: the caller transfers ownership of the temporary plaintext
        # buffer to this function. Always wipe it, including on write failure.
        wipe_secret_buffer(
            token
        )
