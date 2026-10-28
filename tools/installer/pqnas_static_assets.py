#!/usr/bin/env python3
"""
Secure installation of root-managed PQ-NAS static web assets.
"""

from __future__ import annotations

import os
import shutil
import stat


def _validate_static_asset_tree(
    root: str,
) -> None:
    """
    Accept only a real directory tree containing regular files
    and real directories.
    """
    try:
        root_mode = os.lstat(root).st_mode
    except OSError as exc:
        raise RuntimeError(
            "Failed to inspect static asset root"
        ) from exc

    if not stat.S_ISDIR(root_mode):
        raise RuntimeError(
            f"Static asset root is not a real directory: {root}"
        )

    for current_root, dir_names, file_names in os.walk(
        root,
        topdown=True,
        followlinks=False,
    ):
        for name in dir_names:
            path = os.path.join(current_root, name)

            try:
                mode = os.lstat(path).st_mode
            except OSError as exc:
                raise RuntimeError(
                    "Failed to inspect static asset entry"
                ) from exc

            # Security: never follow packaged symlinks or accept
            # special files while installing root-managed assets.
            if not stat.S_ISDIR(mode):
                raise RuntimeError(
                    f"Unsupported static asset entry: {path}"
                )

        for name in file_names:
            path = os.path.join(current_root, name)

            try:
                mode = os.lstat(path).st_mode
            except OSError as exc:
                raise RuntimeError(
                    "Failed to inspect static asset entry"
                ) from exc

            if not stat.S_ISREG(mode):
                raise RuntimeError(
                    f"Unsupported static asset entry: {path}"
                )


def _remove_static_destination_entry(
    path: str,
) -> None:
    try:
        mode = os.lstat(path).st_mode
    except FileNotFoundError:
        return

    if stat.S_ISDIR(mode):
        shutil.rmtree(path)
    else:
        os.unlink(path)


def _copy_static_file(
    source: str,
    destination: str,
) -> str:
    # Security: copy contents without preserving package-controlled
    # ownership or restrictive staging mode bits.
    shutil.copyfile(
        source,
        destination,
        follow_symlinks=False,
    )
    return destination


def _normalize_static_asset_modes(
    root: str,
) -> None:
    """
    Make installed web assets readable by the unprivileged
    pqnas service while keeping them root-managed and non-writable.
    """
    try:
        root_mode = os.lstat(root).st_mode
    except OSError as exc:
        raise RuntimeError(
            "Failed to inspect installed static asset"
        ) from exc

    if stat.S_ISREG(root_mode):
        os.chmod(root, 0o644)  # nosemgrep: python.lang.security.audit.insecure-file-permissions.insecure-file-permissions - Security-reviewed: public root-owned web asset must be readable by the unprivileged pqnas service and is not group/world-writable.
        return

    if not stat.S_ISDIR(root_mode):
        raise RuntimeError(
            f"Unsupported installed static asset: {root}"
        )

    for current_root, dir_names, file_names in os.walk(
        root,
        topdown=True,
        followlinks=False,
    ):
        current_mode = os.lstat(current_root).st_mode

        if not stat.S_ISDIR(current_mode):
            raise RuntimeError(
                f"Unsupported installed static directory: "
                f"{current_root}"
            )

        os.chmod(current_root, 0o755)  # nosemgrep: python.lang.security.audit.insecure-file-permissions.insecure-file-permissions - Security-reviewed: root-owned static directory needs traversal by the unprivileged pqnas service and is not group/world-writable.

        for name in dir_names:
            path = os.path.join(current_root, name)
            mode = os.lstat(path).st_mode

            if not stat.S_ISDIR(mode):
                raise RuntimeError(
                    f"Unsupported installed static asset: {path}"
                )

        for name in file_names:
            path = os.path.join(current_root, name)
            mode = os.lstat(path).st_mode

            if not stat.S_ISREG(mode):
                raise RuntimeError(
                    f"Unsupported installed static asset: {path}"
                )

            os.chmod(path, 0o644)  # nosemgrep: python.lang.security.audit.insecure-file-permissions.insecure-file-permissions - Security-reviewed: public root-owned web asset must be readable by the unprivileged pqnas service and is not group/world-writable.


def install_static_assets(
    asset_root: str,
    dest_root: str = "/opt/pqnas/static",
) -> None:
    """
    Copy static web assets into /opt/pqnas/static.

    repo mode:    <repo>/server/src/static/*
    package mode: <pkg>/static/*
    """
    pkg = os.path.join(asset_root, "static")
    repo = os.path.join(
        asset_root,
        "server",
        "src",
        "static",
    )

    src = pkg if os.path.isdir(pkg) else repo

    if not os.path.isdir(src):
        raise RuntimeError(
            f"Missing static dir (tried): {pkg} and {repo}"
        )

    _validate_static_asset_tree(src)

    os.makedirs(dest_root, exist_ok=True)

    destination_mode = os.lstat(dest_root).st_mode

    if not stat.S_ISDIR(destination_mode):
        raise RuntimeError(
            "Static asset destination is not a real directory"
        )

    os.chmod(dest_root, 0o755)  # nosemgrep: python.lang.security.audit.insecure-file-permissions.insecure-file-permissions - Security-reviewed: root-owned static directory needs traversal by the unprivileged pqnas service and is not group/world-writable.

    for name in sorted(os.listdir(src)):
        source = os.path.join(src, name)
        destination = os.path.join(dest_root, name)
        source_mode = os.lstat(source).st_mode

        _remove_static_destination_entry(destination)

        try:
            if stat.S_ISDIR(source_mode):
                shutil.copytree(
                    source,
                    destination,
                    symlinks=True,
                    copy_function=_copy_static_file,
                )
            elif stat.S_ISREG(source_mode):
                _copy_static_file(
                    source,
                    destination,
                )
            else:
                raise RuntimeError(
                    "Unsupported static asset entry: "
                    f"{source}"
                )

            _normalize_static_asset_modes(destination)

        except Exception:
            # Security: never leave a rejected or partially copied
            # static entry in the root-managed serving directory.
            _remove_static_destination_entry(destination)
            raise
