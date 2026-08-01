#!/usr/bin/env python3
# Descriptor-based mutable runtime config handling for the root installer.

from __future__ import annotations

import os
import pwd
import secrets
import stat
from pathlib import Path
from typing import Callable, Optional


MAX_MANAGED_FILE_BYTES = 1024 * 1024
MAX_PATH_BYTES = 4096


class MutableConfigError(RuntimeError):
    pass


def service_owner_ids(user: str = "pqnas") -> tuple[int, int]:
    try:
        record = pwd.getpwnam(user)
    except KeyError as exc:
        raise MutableConfigError(
            "mutable config service account does not exist"
        ) from exc

    return record.pw_uid, record.pw_gid


def _normalized_absolute_path(path: str) -> Path:
    if (
        not isinstance(path, str)
        or not path
        or "\x00" in path
        or len(path.encode("utf-8")) > MAX_PATH_BYTES
        or not os.path.isabs(path)
    ):
        raise MutableConfigError(
            "mutable config path must be a bounded absolute path"
        )

    normalized = os.path.normpath(path)
    target = Path(normalized)

    if normalized != path or normalized == "/":
        raise MutableConfigError(
            "mutable config path must be normalized and non-root"
        )

    for component in target.parts:
        if component in (".", ".."):
            raise MutableConfigError(
                "mutable config path contains a dot component"
            )

    return target


def _directory_flags() -> int:
    return (
        os.O_RDONLY
        | os.O_CLOEXEC
        | os.O_DIRECTORY
        | getattr(os, "O_NOFOLLOW", 0)
    )


def _open_directory_path(path: str) -> int:
    target = _normalized_absolute_path(path)

    try:
        descriptor = os.open("/", _directory_flags())
    except OSError as exc:
        raise MutableConfigError(
            "failed to open filesystem root safely"
        ) from exc

    try:
        for component in target.parts[1:]:
            try:
                next_descriptor = os.open(
                    component,
                    _directory_flags(),
                    dir_fd=descriptor,
                )
            except OSError as exc:
                raise MutableConfigError(
                    "failed to open mutable directory path safely"
                ) from exc

            os.close(descriptor)
            descriptor = next_descriptor

        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def _validate_directory_metadata(
    metadata: os.stat_result,
    *,
    allowed_uids: frozenset[int],
    expected_gid: int,
) -> None:
    if (
        not stat.S_ISDIR(metadata.st_mode)
        or metadata.st_uid not in allowed_uids
        or metadata.st_gid not in (0, expected_gid)
        or stat.S_IMODE(metadata.st_mode) & 0o022
    ):
        raise MutableConfigError(
            "mutable config directory has unsafe metadata"
        )


STORAGE_RUNTIME_DIRECTORIES = (
    "data",
    "logs",
    "apps",
    "apps/bundled",
    "apps/installed",
    "apps/users",
    "audit",
    "tmp",
    "cache",
    "cache/gallery_thumbs",
    "cache/reelstack_thumbs",
    "cache/reelstack_meta",
    "cache/reelstack_index",
    "cache/music_covers",
    "cache/workspace_file_thumbs",
    "cache/share_posters",
)


def _relative_directory_components(
    relative_path: str,
) -> tuple[str, ...]:
    if (
        not isinstance(relative_path, str)
        or not relative_path
        or "\x00" in relative_path
        or os.path.isabs(relative_path)
        or os.path.normpath(relative_path) != relative_path
    ):
        raise MutableConfigError(
            "runtime directory path must be normalized and relative"
        )

    components = tuple(relative_path.split("/"))

    if any(
        not component or component in (".", "..")
        for component in components
    ):
        raise MutableConfigError(
            "runtime directory path contains an unsafe component"
        )

    return components


def _open_or_create_managed_child_directory(
    parent_fd: int,
    name: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> int:
    try:
        initial = os.stat(
            name,
            dir_fd=parent_fd,
            follow_symlinks=False,
        )
    except FileNotFoundError:
        initial = None
    except OSError as exc:
        raise MutableConfigError(
            "failed to inspect runtime directory safely"
        ) from exc

    if initial is None:
        try:
            os.mkdir(
                name,
                mode=0o750,
                dir_fd=parent_fd,
            )
        except OSError as exc:
            raise MutableConfigError(
                "failed to create runtime directory safely"
            ) from exc

        try:
            initial = os.stat(
                name,
                dir_fd=parent_fd,
                follow_symlinks=False,
            )
        except OSError as exc:
            raise MutableConfigError(
                "failed to inspect created runtime directory"
            ) from exc
        os.fsync(parent_fd)

    _validate_directory_metadata(
        initial,
        allowed_uids=frozenset((0, owner_uid)),
        expected_gid=owner_gid,
    )

    try:
        descriptor = os.open(
            name,
            _directory_flags(),
            dir_fd=parent_fd,
        )
    except OSError as exc:
        raise MutableConfigError(
            "failed to open runtime directory safely"
        ) from exc

    try:
        opened = os.fstat(descriptor)
        _validate_directory_metadata(
            opened,
            allowed_uids=frozenset((0, owner_uid)),
            expected_gid=owner_gid,
        )

        if (
            opened.st_dev != initial.st_dev
            or opened.st_ino != initial.st_ino
        ):
            raise MutableConfigError(
                "runtime directory changed during validation"
            )

        # Security: change ownership on the opened directory inode only.
        # Never recursively traverse user data or follow hardlinks.
        os.fchown(descriptor, owner_uid, owner_gid)
        os.fchmod(descriptor, 0o750)
        os.fsync(descriptor)
        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def prepare_storage_runtime_layout(
    storage_root: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> None:
    root = _normalized_absolute_path(storage_root)
    root_fd = _open_directory_path(str(root))

    try:
        root_metadata = os.fstat(root_fd)
        _validate_directory_metadata(
            root_metadata,
            allowed_uids=frozenset((0, owner_uid)),
            expected_gid=owner_gid,
        )

        for relative_path in STORAGE_RUNTIME_DIRECTORIES:
            current_fd = os.dup(root_fd)

            try:
                for component in _relative_directory_components(
                    relative_path
                ):
                    child_fd = _open_or_create_managed_child_directory(
                        current_fd,
                        component,
                        owner_uid=owner_uid,
                        owner_gid=owner_gid,
                    )
                    os.close(current_fd)
                    current_fd = child_fd
            finally:
                os.close(current_fd)

        # Keep the mountpoint itself private and service-owned, but do not
        # recursively alter any existing file or directory below it.
        os.fchown(root_fd, owner_uid, owner_gid)
        os.fchmod(root_fd, 0o750)
        os.fsync(root_fd)
    finally:
        os.close(root_fd)


def prepare_service_state_directory(
    path: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> str:
    target = _normalized_absolute_path(path)
    parent = str(target.parent)
    parent_fd = _open_directory_path(parent)
    child_fd = -1

    try:
        parent_metadata = os.fstat(parent_fd)
        _validate_directory_metadata(
            parent_metadata,
            allowed_uids=frozenset((0, owner_uid)),
            expected_gid=owner_gid,
        )

        child_fd = _open_or_create_managed_child_directory(
            parent_fd,
            target.name,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )
        return str(target)
    finally:
        if child_fd >= 0:
            os.close(child_fd)
        os.close(parent_fd)


def prepare_mutable_config_directory(
    storage_root: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> str:
    root = _normalized_absolute_path(storage_root)

    root_fd = _open_directory_path(str(root))

    config_fd = -1

    try:
        root_metadata = os.fstat(root_fd)
        _validate_directory_metadata(
            root_metadata,
            allowed_uids=frozenset((0, owner_uid)),
            expected_gid=owner_gid,
        )

        # Security: only the storage root becomes service-owned. Root-managed
        # code, trust, and environment paths are outside this directory.
        os.fchown(root_fd, owner_uid, owner_gid)
        os.fchmod(root_fd, 0o750)

        try:
            initial = os.stat(
                "config",
                dir_fd=root_fd,
                follow_symlinks=False,
            )
        except FileNotFoundError:
            initial = None
        except OSError as exc:
            raise MutableConfigError(
                "failed to inspect mutable config directory"
            ) from exc

        if initial is None:
            try:
                os.mkdir(
                    "config",
                    mode=0o750,
                    dir_fd=root_fd,
                )
            except OSError as exc:
                raise MutableConfigError(
                    "failed to create mutable config directory"
                ) from exc
            os.fsync(root_fd)
        else:
            _validate_directory_metadata(
                initial,
                allowed_uids=frozenset((0, owner_uid)),
                expected_gid=owner_gid,
            )

        try:
            config_fd = os.open(
                "config",
                _directory_flags(),
                dir_fd=root_fd,
            )
        except OSError as exc:
            raise MutableConfigError(
                "failed to open mutable config directory safely"
            ) from exc

        config_metadata = os.fstat(config_fd)
        _validate_directory_metadata(
            config_metadata,
            allowed_uids=frozenset((0, owner_uid)),
            expected_gid=owner_gid,
        )

        if (
            initial is not None
            and (
                initial.st_dev != config_metadata.st_dev
                or initial.st_ino != config_metadata.st_ino
            )
        ):
            raise MutableConfigError(
                "mutable config directory changed during validation"
            )

        os.fchown(config_fd, owner_uid, owner_gid)
        os.fchmod(config_fd, 0o750)
        os.fsync(config_fd)

        return str(root / "config")
    finally:
        if config_fd >= 0:
            os.close(config_fd)
        os.close(root_fd)


def _normalized_file_target(path: str) -> tuple[str, str]:
    target = _normalized_absolute_path(path)

    if not target.name:
        raise MutableConfigError(
            "mutable config path must name a file"
        )

    return str(target.parent), target.name


def _open_managed_parent(
    parent: str,
    *,
    owner_uid: int,
    owner_gid: int,
) -> int:
    descriptor = _open_directory_path(parent)

    try:
        metadata = os.fstat(descriptor)
        _validate_directory_metadata(
            metadata,
            allowed_uids=frozenset((owner_uid,)),
            expected_gid=owner_gid,
        )
        return descriptor
    except Exception:
        os.close(descriptor)
        raise


def _validate_file_metadata(
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
        or metadata.st_size < 0
        or metadata.st_size > MAX_MANAGED_FILE_BYTES
    ):
        raise MutableConfigError(
            "mutable config file has unsafe metadata"
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
        raise MutableConfigError(
            "failed to inspect mutable config file"
        ) from exc

    _validate_file_metadata(
        metadata,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )
    return metadata


def _read_open_file(
    descriptor: int,
) -> str:
    chunks = []
    total = 0

    while True:
        try:
            chunk = os.read(
                descriptor,
                min(
                    8192,
                    MAX_MANAGED_FILE_BYTES + 1 - total,
                ),
            )
        except InterruptedError:
            continue
        except OSError as exc:
            raise MutableConfigError(
                "failed to read mutable config file"
            ) from exc

        if not chunk:
            break

        chunks.append(chunk)
        total += len(chunk)

        if total > MAX_MANAGED_FILE_BYTES:
            raise MutableConfigError(
                "mutable config file exceeds the size limit"
            )

    try:
        return b"".join(chunks).decode(
            "utf-8",
            errors="strict",
        )
    except UnicodeDecodeError as exc:
        raise MutableConfigError(
            "mutable config file is not valid UTF-8"
        ) from exc


def read_managed_text(
    path: str,
    *,
    owner_uid: int,
    owner_gid: int,
    file_mode: int,
) -> Optional[str]:
    parent, filename = _normalized_file_target(path)
    directory_fd = _open_managed_parent(
        parent,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )
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
            raise MutableConfigError(
                "failed to open mutable config file safely"
            ) from exc

        metadata = os.fstat(file_fd)
        _validate_file_metadata(
            metadata,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        # Security: repair legacy permissive modes only after opening and
        # validating the actual inode without following links.
        os.fchmod(file_fd, file_mode)
        return _read_open_file(file_fd)
    finally:
        if file_fd >= 0:
            os.close(file_fd)
        os.close(directory_fd)


def read_packaged_text(path: str) -> str:
    target = _normalized_absolute_path(path)
    flags = (
        os.O_RDONLY
        | os.O_CLOEXEC
        | getattr(os, "O_NOFOLLOW", 0)
    )

    try:
        descriptor = os.open(str(target), flags)
    except OSError as exc:
        raise MutableConfigError(
            "failed to open packaged config safely"
        ) from exc

    try:
        metadata = os.fstat(descriptor)
        if (
            not stat.S_ISREG(metadata.st_mode)
            or metadata.st_nlink != 1
            or metadata.st_size < 0
            or metadata.st_size > MAX_MANAGED_FILE_BYTES
        ):
            raise MutableConfigError(
                "packaged config has unsafe metadata"
            )
        return _read_open_file(descriptor)
    finally:
        os.close(descriptor)


def _encode_text(text: str) -> bytes:
    if not isinstance(text, str):
        raise MutableConfigError(
            "mutable config contents must be text"
        )

    try:
        contents = text.encode("utf-8", errors="strict")
    except UnicodeEncodeError as exc:
        raise MutableConfigError(
            "mutable config contents are not valid UTF-8"
        ) from exc

    if len(contents) > MAX_MANAGED_FILE_BYTES:
        raise MutableConfigError(
            "mutable config contents exceed the size limit"
        )

    return contents


def _write_all(descriptor: int, contents: bytes) -> None:
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
            raise MutableConfigError(
                "failed to write mutable config file"
            ) from exc

        if written <= 0:
            raise MutableConfigError(
                "short write while writing mutable config file"
            )

        offset += written


def atomic_write_managed_text(
    path: str,
    text: str,
    *,
    owner_uid: int,
    owner_gid: int,
    file_mode: int,
    replace_existing: bool = True,
) -> bool:
    if file_mode not in (0o600, 0o640):
        raise MutableConfigError(
            "invalid mutable config file mode"
        )

    contents = _encode_text(text)
    parent, filename = _normalized_file_target(path)
    directory_fd = _open_managed_parent(
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
            file_fd = -1
            try:
                file_fd = os.open(
                    filename,
                    (
                        os.O_RDONLY
                        | os.O_CLOEXEC
                        | getattr(os, "O_NOFOLLOW", 0)
                    ),
                    dir_fd=directory_fd,
                )
                current = os.fstat(file_fd)
                _validate_file_metadata(
                    current,
                    owner_uid=owner_uid,
                    owner_gid=owner_gid,
                )
                os.fchmod(file_fd, file_mode)
                _read_open_file(file_fd)
            finally:
                if file_fd >= 0:
                    os.close(file_fd)
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
                    file_mode,
                    dir_fd=directory_fd,
                )
                temporary_name = candidate
                temporary_created = True
                break
            except FileExistsError:
                continue
            except OSError as exc:
                raise MutableConfigError(
                    "failed to create mutable config temp file"
                ) from exc

        if temporary_fd < 0:
            raise MutableConfigError(
                "failed to reserve mutable config temp file"
            )

        os.fchown(temporary_fd, owner_uid, owner_gid)
        os.fchmod(temporary_fd, file_mode)
        _write_all(temporary_fd, contents)
        os.fsync(temporary_fd)

        written = os.fstat(temporary_fd)
        _validate_file_metadata(
            written,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        if (
            stat.S_IMODE(written.st_mode) != file_mode
            or written.st_size != len(contents)
        ):
            raise MutableConfigError(
                "mutable config temp file failed validation"
            )

        os.close(temporary_fd)
        temporary_fd = -1

        current = _inspect_target(
            directory_fd,
            filename,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        if initial is None:
            if current is not None:
                raise MutableConfigError(
                    "mutable config file changed during creation"
                )
        elif (
            current is None
            or current.st_dev != initial.st_dev
            or current.st_ino != initial.st_ino
        ):
            raise MutableConfigError(
                "mutable config file changed during replacement"
            )

        try:
            os.replace(
                temporary_name,
                filename,
                src_dir_fd=directory_fd,
                dst_dir_fd=directory_fd,
            )
        except OSError as exc:
            raise MutableConfigError(
                "failed to atomically replace mutable config file"
            ) from exc

        temporary_created = False
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
            except OSError:
                pass

        os.close(directory_fd)


def ensure_managed_binary_created(
    path: str,
    writer: Callable[[str], None],
    *,
    owner_uid: int,
    owner_gid: int,
    file_mode: int = 0o600,
) -> bool:
    if not callable(writer):
        raise MutableConfigError(
            "mutable binary writer must be callable"
        )

    if file_mode != 0o600:
        raise MutableConfigError(
            "invalid mutable binary file mode"
        )

    parent, filename = _normalized_file_target(path)
    directory_fd = _open_managed_parent(
        parent,
        owner_uid=owner_uid,
        owner_gid=owner_gid,
    )

    temporary_name = ""
    temporary_created = False
    temporary_fd = -1
    payload_fd = -1

    try:
        initial = _inspect_target(
            directory_fd,
            filename,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        if initial is not None:
            try:
                payload_fd = os.open(
                    filename,
                    (
                        os.O_RDONLY
                        | os.O_CLOEXEC
                        | getattr(os, "O_NOFOLLOW", 0)
                    ),
                    dir_fd=directory_fd,
                )
            except OSError as exc:
                raise MutableConfigError(
                    "failed to open mutable binary safely"
                ) from exc

            current = os.fstat(payload_fd)
            _validate_file_metadata(
                current,
                owner_uid=owner_uid,
                owner_gid=owner_gid,
            )

            if (
                current.st_dev != initial.st_dev
                or current.st_ino != initial.st_ino
            ):
                raise MutableConfigError(
                    "mutable binary changed during validation"
                )

            os.fchmod(payload_fd, file_mode)
            os.fsync(payload_fd)
            return False

        for _ in range(16):
            candidate = (
                f".{filename}.create."
                f"{os.getpid()}."
                f"{secrets.token_hex(8)}"
            )

            try:
                os.mkdir(
                    candidate,
                    mode=0o700,
                    dir_fd=directory_fd,
                )
                temporary_name = candidate
                temporary_created = True
                break
            except FileExistsError:
                continue
            except OSError as exc:
                raise MutableConfigError(
                    "failed to create mutable binary workspace"
                ) from exc

        if not temporary_created:
            raise MutableConfigError(
                "failed to reserve mutable binary workspace"
            )

        try:
            temporary_fd = os.open(
                temporary_name,
                _directory_flags(),
                dir_fd=directory_fd,
            )
        except OSError as exc:
            raise MutableConfigError(
                "failed to open mutable binary workspace"
            ) from exc

        workspace_metadata = os.fstat(temporary_fd)
        if (
            not stat.S_ISDIR(workspace_metadata.st_mode)
            or workspace_metadata.st_nlink < 1
        ):
            raise MutableConfigError(
                "mutable binary workspace is unsafe"
            )

        os.fchown(
            temporary_fd,
            owner_uid,
            owner_gid,
        )
        os.fchmod(temporary_fd, 0o700)

        payload_path = os.path.join(
            parent,
            temporary_name,
            "payload",
        )

        writer(payload_path)

        try:
            entries = os.listdir(temporary_fd)
        except OSError as exc:
            raise MutableConfigError(
                "failed to inspect mutable binary workspace"
            ) from exc

        if entries != ["payload"]:
            raise MutableConfigError(
                "mutable binary writer produced unexpected files"
            )

        try:
            payload_fd = os.open(
                "payload",
                (
                    os.O_RDONLY
                    | os.O_CLOEXEC
                    | getattr(os, "O_NOFOLLOW", 0)
                ),
                dir_fd=temporary_fd,
            )
        except OSError as exc:
            raise MutableConfigError(
                "failed to open generated mutable binary safely"
            ) from exc

        generated = os.fstat(payload_fd)
        _validate_file_metadata(
            generated,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )

        os.fchmod(payload_fd, file_mode)
        os.fsync(payload_fd)

        current = _inspect_target(
            directory_fd,
            filename,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )
        if current is not None:
            raise MutableConfigError(
                "mutable binary changed during creation"
            )

        os.close(payload_fd)
        payload_fd = -1

        try:
            os.replace(
                "payload",
                filename,
                src_dir_fd=temporary_fd,
                dst_dir_fd=directory_fd,
            )
        except OSError as exc:
            raise MutableConfigError(
                "failed to install mutable binary atomically"
            ) from exc

        os.fsync(directory_fd)
        return True
    finally:
        if payload_fd >= 0:
            os.close(payload_fd)

        if temporary_fd >= 0:
            try:
                os.unlink(
                    "payload",
                    dir_fd=temporary_fd,
                )
            except OSError:
                pass
            os.close(temporary_fd)

        if temporary_created and temporary_name:
            try:
                os.rmdir(
                    temporary_name,
                    dir_fd=directory_fd,
                )
            except OSError:
                pass

        os.close(directory_fd)
