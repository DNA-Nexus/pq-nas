#!/usr/bin/env python3
from __future__ import annotations

import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(
    0,
    str(ROOT / "tools" / "installer"),
)

import pqnas_mutable_config as mutable  # noqa: E402


class MutableConfigTests(unittest.TestCase):
    def setUp(self) -> None:
        self.uid = os.geteuid()
        self.gid = os.getegid()

    def prepare(self, root: Path) -> Path:
        root.mkdir(mode=0o750)
        root.chmod(0o750)
        path = mutable.prepare_mutable_config_directory(
            str(root),
            owner_uid=self.uid,
            owner_gid=self.gid,
        )
        return Path(path)

    def test_prepare_directory_and_atomic_text(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "users.json"

            self.assertTrue(
                mutable.atomic_write_managed_text(
                    str(target),
                    '{"users": []}\n',
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                    file_mode=0o640,
                    replace_existing=False,
                )
            )
            self.assertEqual(
                mutable.read_managed_text(
                    str(target),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                    file_mode=0o640,
                ),
                '{"users": []}\n',
            )
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o640,
            )

    def test_storage_root_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            real = base / "real"
            real.mkdir(mode=0o750)
            linked = base / "linked"
            linked.symlink_to(real, target_is_directory=True)

            with self.assertRaises(mutable.MutableConfigError):
                mutable.prepare_mutable_config_directory(
                    str(linked),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

    def test_config_directory_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "storage"
            root.mkdir(mode=0o750)
            outside = Path(directory) / "outside"
            outside.mkdir(mode=0o750)
            (root / "config").symlink_to(
                outside,
                target_is_directory=True,
            )

            with self.assertRaises(mutable.MutableConfigError):
                mutable.prepare_mutable_config_directory(
                    str(root),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

    def test_target_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            config = self.prepare(base / "storage")
            outside = base / "outside"
            outside.write_text("keep\n", encoding="utf-8")
            target = config / "policy.json"
            target.symlink_to(outside)

            with self.assertRaises(mutable.MutableConfigError):
                mutable.atomic_write_managed_text(
                    str(target),
                    "{}\n",
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                    file_mode=0o640,
                )

            self.assertEqual(
                outside.read_text(encoding="utf-8"),
                "keep\n",
            )

    def test_hardlinked_target_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "policy.json"
            alias = config / "alias.json"
            target.write_text("{}\n", encoding="utf-8")
            target.chmod(0o640)
            os.link(target, alias)

            with self.assertRaises(mutable.MutableConfigError):
                mutable.read_managed_text(
                    str(target),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                    file_mode=0o640,
                )

    def test_existing_mode_is_hardened_without_replacement(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "policy.json"
            target.write_text("{}\n", encoding="utf-8")
            target.chmod(0o664)
            inode = target.stat().st_ino

            self.assertFalse(
                mutable.atomic_write_managed_text(
                    str(target),
                    '{"replacement": true}\n',
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                    file_mode=0o640,
                    replace_existing=False,
                )
            )

            self.assertEqual(target.stat().st_ino, inode)
            self.assertEqual(
                target.read_text(encoding="utf-8"),
                "{}\n",
            )
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o640,
            )


    def test_managed_binary_writer_creates_atomically(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "opaque_server_setup.bin"

            def writer(path: str) -> None:
                Path(path).write_bytes(b"opaque-setup")
                Path(path).chmod(0o600)

            self.assertTrue(
                mutable.ensure_managed_binary_created(
                    str(target),
                    writer,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )
            self.assertEqual(
                target.read_bytes(),
                b"opaque-setup",
            )
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o600,
            )

    def test_managed_binary_writer_reopens_stale_workspace_fd(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "opaque_server_setup.bin"

            real_open = os.open
            real_listdir = os.listdir
            workspace_fds = []

            def tracked_open(
                path,
                flags,
                mode=0o777,
                *,
                dir_fd=None,
            ):
                descriptor = real_open(
                    path,
                    flags,
                    mode,
                    dir_fd=dir_fd,
                )

                if (
                    isinstance(path, str)
                    and path.startswith(
                        ".opaque_server_setup.bin.create."
                    )
                    and flags & os.O_DIRECTORY
                ):
                    workspace_fds.append(descriptor)

                return descriptor

            def stale_first_workspace_listdir(path):
                if (
                    isinstance(path, int)
                    and workspace_fds
                    and path == workspace_fds[0]
                ):
                    return []

                return real_listdir(path)

            def writer(path: str) -> None:
                Path(path).write_bytes(b"opaque-setup")
                Path(path).chmod(0o600)

            with (
                mock.patch.object(
                    mutable.os,
                    "open",
                    side_effect=tracked_open,
                ),
                mock.patch.object(
                    mutable.os,
                    "listdir",
                    side_effect=stale_first_workspace_listdir,
                ),
            ):
                self.assertTrue(
                    mutable.ensure_managed_binary_created(
                        str(target),
                        writer,
                        owner_uid=self.uid,
                        owner_gid=self.gid,
                    )
                )

            # The first descriptor simulates the stale Btrfs directory view.
            # A second descriptor must be opened after the writer completes.
            self.assertGreaterEqual(len(workspace_fds), 2)
            self.assertEqual(
                target.read_bytes(),
                b"opaque-setup",
            )

    def test_managed_binary_writer_rejects_replaced_workspace(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "opaque_server_setup.bin"

            def writer(path: str) -> None:
                workspace = Path(path).parent
                displaced = workspace.with_name(
                    workspace.name + ".displaced"
                )

                workspace.rename(displaced)
                workspace.mkdir(mode=0o700)
                Path(path).write_bytes(b"replacement")
                Path(path).chmod(0o600)

            with self.assertRaisesRegex(
                mutable.MutableConfigError,
                "workspace changed during generation",
            ):
                mutable.ensure_managed_binary_created(
                    str(target),
                    writer,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

            self.assertFalse(target.exists())

    def test_managed_binary_writer_rejects_symlink_payload(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            config = self.prepare(base / "storage")
            target = config / "opaque_server_setup.bin"
            outside = base / "outside"
            outside.write_bytes(b"keep")

            def writer(path: str) -> None:
                Path(path).symlink_to(outside)

            with self.assertRaises(mutable.MutableConfigError):
                mutable.ensure_managed_binary_created(
                    str(target),
                    writer,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

            self.assertFalse(target.exists())
            self.assertEqual(outside.read_bytes(), b"keep")

    def test_existing_managed_binary_is_preserved(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "opaque_server_setup.bin"
            target.write_bytes(b"existing")
            target.chmod(0o644)
            inode = target.stat().st_ino

            def writer(path: str) -> None:
                raise AssertionError(
                    "writer must not run for existing setup"
                )

            self.assertFalse(
                mutable.ensure_managed_binary_created(
                    str(target),
                    writer,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )
            self.assertEqual(target.stat().st_ino, inode)
            self.assertEqual(target.read_bytes(), b"existing")
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o600,
            )


    def test_storage_runtime_layout_creates_known_directories(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "storage"
            root.mkdir(mode=0o750)
            root.chmod(0o750)

            mutable.prepare_storage_runtime_layout(
                str(root),
                owner_uid=self.uid,
                owner_gid=self.gid,
            )

            for relative in mutable.STORAGE_RUNTIME_DIRECTORIES:
                target = root / relative
                self.assertTrue(target.is_dir(), relative)
                self.assertEqual(
                    stat.S_IMODE(target.stat().st_mode),
                    0o750,
                )

    def test_storage_runtime_layout_rejects_nested_symlink(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            root = base / "storage"
            root.mkdir(mode=0o750)
            root.chmod(0o750)
            outside = base / "outside"
            outside.mkdir(mode=0o750)
            (root / "cache").symlink_to(
                outside,
                target_is_directory=True,
            )

            with self.assertRaises(mutable.MutableConfigError):
                mutable.prepare_storage_runtime_layout(
                    str(root),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

            self.assertEqual(list(outside.iterdir()), [])

    def test_storage_runtime_layout_rejects_intermediate_symlink(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            real_parent = base / "real"
            real_parent.mkdir(mode=0o750)
            storage = real_parent / "storage"
            storage.mkdir(mode=0o750)
            linked_parent = base / "linked"
            linked_parent.symlink_to(
                real_parent,
                target_is_directory=True,
            )

            with self.assertRaises(mutable.MutableConfigError):
                mutable.prepare_storage_runtime_layout(
                    str(linked_parent / "storage"),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

    def test_service_state_directory_is_non_recursive(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "state"
            parent.mkdir(mode=0o750)
            parent.chmod(0o750)
            target = parent / "pqnas"

            result = mutable.prepare_service_state_directory(
                str(target),
                owner_uid=self.uid,
                owner_gid=self.gid,
            )

            self.assertEqual(result, str(target))
            self.assertTrue(target.is_dir())
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o750,
            )

    def test_service_state_directory_supports_private_mode(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "state"
            parent.mkdir(mode=0o750)
            parent.chmod(0o750)

            target = parent / "cluster"
            target.mkdir(mode=0o750)
            target.chmod(0o750)

            result = mutable.prepare_service_state_directory(
                str(target),
                owner_uid=self.uid,
                owner_gid=self.gid,
                directory_mode=0o700,
            )

            self.assertEqual(result, str(target))
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o700,
            )

    def test_atomic_replace_changes_inode(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            config = self.prepare(Path(directory) / "storage")
            target = config / "admin_settings.json"

            mutable.atomic_write_managed_text(
                str(target),
                '{"first": true}\n',
                owner_uid=self.uid,
                owner_gid=self.gid,
                file_mode=0o640,
            )
            first_inode = target.stat().st_ino

            mutable.atomic_write_managed_text(
                str(target),
                '{"second": true}\n',
                owner_uid=self.uid,
                owner_gid=self.gid,
                file_mode=0o640,
            )

            self.assertNotEqual(target.stat().st_ino, first_inode)
            self.assertEqual(
                target.read_text(encoding="utf-8"),
                '{"second": true}\n',
            )


if __name__ == "__main__":
    unittest.main()
