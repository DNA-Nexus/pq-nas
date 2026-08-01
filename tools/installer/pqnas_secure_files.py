#!/usr/bin/env python3
# Descriptor-based private file writes for the root installer.

from __future__ import annotations

import os
import secrets
import stat
from pathlib import Path
from typing import Optional

MAX_PRIVATE_FILE_BYTES = 1024 * 1024
MAX_PRIVATE_ENV_VALUE_BYTES = 4096
MAX_PATH_BYTES = 4096


class SecurePrivateFileError(RuntimeError):
    pass


def _normalized_target(path: str) -> tuple[str, str]:
    if (
        not isinstance(path, str)
        or not path
        or len(path.encode("utf-8")) > MAX_PATH_BYTES
        or "\x00" in path
        or not os.path.isabs(path)
    ):
        raise SecurePrivateFileError(
            "private file path must be a bounded absolute path"
        )

    normalized = os.path.normpath(path)
    target = Path(normalized)

    if (
        normalized != path
        or normalized == "/"
        or not target.name
    ):
        raise SecurePrivateFileError(
            "private file path must name a normalized file"
        )

    for component in target.parts:
        if component in (".", ".."):
            raise SecurePrivateFileError(
                "private file path contains a dot component"
            )

    return str(target.parent), target.name


def _open_private_parent(
    parent: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> int:
    flags = (
        os.O_RDONLY
        | os.O_CLOEXEC
        | os.O_DIRECTORY
        | getattr(os, "O_NOFOLLOW", 0)
    )

    try:
        descriptor = os.open(parent, flags)
    except OSError as exc:
        raise SecurePrivateFileError(
            "failed to open private file directory safely"
        ) from exc

    try:
        metadata = os.fstat(descriptor)

        if (
            not stat.S_ISDIR(metadata.st_mode)
            or metadata.st_uid != owner_uid
            or metadata.st_gid != owner_gid
            or stat.S_IMODE(metadata.st_mode) & 0o022
        ):
            raise SecurePrivateFileError(
                "private file directory has unsafe metadata"
            )

        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def _validate_private_metadata(
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
        or metadata.st_size > MAX_PRIVATE_FILE_BYTES
    ):
        raise SecurePrivateFileError(
            "private file has unsafe metadata"
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
        raise SecurePrivateFileError(
            "failed to inspect private file"
        ) from exc

    _validate_private_metadata(
        metadata,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )
    return metadata


def private_file_exists(
    path: str,
    *,
    owner_uid: int = 0,
    owner_gid: int = 0,
) -> bool:
    parent, filename = _normalized_target(path)
    directory_fd = _open_private_parent(
        parent,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )

    try:
        return (
            _inspect_target(
                directory_fd,
                filename,
                owner_uid=owner_uid,
                owner_gid=owner_gid,
            )
            is not None
        )
    finally:
        os.close(directory_fd)


def read_private_file_text(
    path: str,
    *,
    owner_uid: int = 0,
    owner_gid: int = 0,
) -> Optional[str]:
    parent, filename = _normalized_target(path)

    try:
        directory_fd = _open_private_parent(
            parent,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )
    except SecurePrivateFileError as exc:
        # A fresh install may not have created the private config directory
        # yet. Only a genuine missing path is treated as an absent optional
        # file; symlinks and unsafe metadata continue to fail closed.
        if isinstance(exc.__cause__, FileNotFoundError):
            return None
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
            return None
        except OSError as exc:
            raise SecurePrivateFileError(
                "failed to open private file safely"
            ) from exc

        metadata = os.fstat(file_fd)
        _validate_private_metadata(
            metadata,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        chunks = []
        total = 0

        while True:
            try:
                chunk = os.read(
                    file_fd,
                    min(
                        8192,
                        MAX_PRIVATE_FILE_BYTES + 1 - total,
                    ),
                )
            except InterruptedError:
                continue
            except OSError as exc:
                raise SecurePrivateFileError(
                    "failed to read private file"
                ) from exc

            if not chunk:
                break

            chunks.append(chunk)
            total += len(chunk)

            if total > MAX_PRIVATE_FILE_BYTES:
                raise SecurePrivateFileError(
                    "private file exceeds the size limit"
                )

        try:
            return b"".join(chunks).decode(
                "utf-8",
                errors="strict",
            )
        except UnicodeDecodeError as exc:
            raise SecurePrivateFileError(
                "private file is not valid UTF-8"
            ) from exc
    finally:
        if file_fd >= 0:
            os.close(file_fd)
        os.close(directory_fd)


def validate_exact_private_env_keys(
    text: str,
    required_keys,
) -> None:
    if (
        not isinstance(text, str)
        or len(text.encode("utf-8")) > MAX_PRIVATE_FILE_BYTES
        or "\x00" in text
    ):
        raise SecurePrivateFileError(
            "private environment file has invalid contents"
        )

    required = frozenset(required_keys)

    if (
        not required
        or any(
            not isinstance(key, str)
            or not key
            or any(
                not (
                    character == "_"
                    or "A" <= character <= "Z"
                    or "0" <= character <= "9"
                )
                for character in key
            )
            for key in required
        )
    ):
        raise SecurePrivateFileError(
            "invalid required private environment keys"
        )

    seen = set()

    for line_number, raw_line in enumerate(
        text.splitlines(),
        start=1,
    ):
        if not raw_line:
            raise SecurePrivateFileError(
                "private environment file contains a blank line"
            )

        if "=" not in raw_line:
            raise SecurePrivateFileError(
                f"malformed private environment entry on line {line_number}"
            )

        key, value = raw_line.split("=", 1)

        if key not in required:
            raise SecurePrivateFileError(
                f"unexpected private environment key on line {line_number}"
            )

        if key in seen:
            raise SecurePrivateFileError(
                f"duplicate private environment key on line {line_number}"
            )

        unpadded_value = value.rstrip("=")
        padding_bytes = len(value) - len(unpadded_value)

        if (
            not unpadded_value
            or len(value.encode("utf-8"))
                > MAX_PRIVATE_ENV_VALUE_BYTES
            or padding_bytes > 2
            or "=" in unpadded_value
            or any(
                not (
                    character == "-"
                    or character == "_"
                    or "A" <= character <= "Z"
                    or "a" <= character <= "z"
                    or "0" <= character <= "9"
                )
                for character in unpadded_value
            )
        ):
            # Security: never include the rejected secret value in errors.
            raise SecurePrivateFileError(
                f"invalid private environment value on line {line_number}"
            )

        seen.add(key)

    if seen != required:
        raise SecurePrivateFileError(
            "private environment file is missing required keys"
        )


def _write_all(
    descriptor: int,
    contents: bytes,
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
            raise SecurePrivateFileError(
                "failed to write private file"
            ) from exc

        if written <= 0:
            raise SecurePrivateFileError(
                "short write while writing private file"
            )

        offset += written


def atomic_write_private_file(
    path: str,
    text: str,
    *,
    owner_uid: int = 0,
    owner_gid: int = 0,
    replace_existing: bool = True,
) -> bool:
    # Returns False only when create-only mode sees an existing safe target.
    if not isinstance(text, str):
        raise SecurePrivateFileError(
            "private file contents must be text"
        )

    try:
        contents = text.encode(
            "utf-8",
            errors="strict",
        )
    except UnicodeEncodeError as exc:
        raise SecurePrivateFileError(
            "private file contents are not valid UTF-8"
        ) from exc

    if len(contents) > MAX_PRIVATE_FILE_BYTES:
        raise SecurePrivateFileError(
            "private file contents exceed the size limit"
        )

    parent, filename = _normalized_target(path)
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

        if initial is not None and not replace_existing:
            return False

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
                raise SecurePrivateFileError(
                    "failed to create private temp file"
                ) from exc

        if temporary_fd < 0:
            raise SecurePrivateFileError(
                "failed to reserve a unique private temp file"
            )

        # Security: set final ownership and permissions on the opened inode
        # before writing secret or security-sensitive contents.
        os.fchmod(temporary_fd, 0o600)
        os.fchown(
            temporary_fd,
            owner_uid,
            owner_gid,
        )

        _write_all(
            temporary_fd,
            contents,
        )
        os.fsync(temporary_fd)

        written_metadata = os.fstat(
            temporary_fd
        )

        if (
            not stat.S_ISREG(written_metadata.st_mode)
            or written_metadata.st_nlink != 1
            or written_metadata.st_uid != owner_uid
            or written_metadata.st_gid != owner_gid
            or stat.S_IMODE(written_metadata.st_mode) != 0o600
            or written_metadata.st_size != len(contents)
        ):
            raise SecurePrivateFileError(
                "private temp file failed metadata validation"
            )

        os.close(temporary_fd)
        temporary_fd = -1

        current = _inspect_target(
            directory_fd,
            filename,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        # Security: fail rather than overwrite a concurrent root-level change.
        if initial is None:
            if current is not None:
                raise SecurePrivateFileError(
                    "private file changed during creation"
                )
        elif (
            current is None
            or current.st_dev != initial.st_dev
            or current.st_ino != initial.st_ino
        ):
            raise SecurePrivateFileError(
                "private file changed during replacement"
            )

        try:
            os.replace(
                temporary_name,
                filename,
                src_dir_fd=directory_fd,
                dst_dir_fd=directory_fd,
            )
        except OSError as exc:
            raise SecurePrivateFileError(
                "failed to atomically replace private file"
            ) from exc

        temporary_created = False

        # Security: the rename is complete only after the directory entry is
        # durably persisted.
        os.fsync(directory_fd)
        return True
    finally:
        if temporary_fd >= 0:
            os.close(temporary_fd)

        if temporary_created and temporary_name:
            try:
                os.unlink(
                    temporary_name,
                    dir_fd=directory_fd,
                )
            except FileNotFoundError:
                pass
            except OSError:
                pass

        os.close(directory_fd)
