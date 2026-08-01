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

import pqnas_secure_files as secure_files  # noqa: E402


class SecurePrivateFileTests(unittest.TestCase):
    def setUp(self) -> None:
        self.owner_uid = os.geteuid()
        self.owner_gid = os.getegid()

    def private_parent(self, root: Path) -> Path:
        parent = root / "private"
        parent.mkdir(mode=0o700)
        parent.chmod(0o700)
        return parent

    def write(
        self,
        path: Path,
        text: str,
        *,
        replace_existing: bool = True,
    ) -> bool:
        return secure_files.atomic_write_private_file(
            str(path),
            text,
            owner_uid=self.owner_uid,
            owner_gid=self.owner_gid,
            replace_existing=replace_existing,
        )

    def test_atomic_create_and_replace(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = self.private_parent(Path(directory))
            target = parent / "pqnas.env"

            self.assertTrue(
                self.write(target, "first\n")
            )
            self.assertEqual(
                target.read_text(encoding="utf-8"),
                "first\n",
            )
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o600,
            )

            first_inode = target.stat().st_ino

            self.assertTrue(
                self.write(target, "second\n")
            )
            self.assertEqual(
                target.read_text(encoding="utf-8"),
                "second\n",
            )
            self.assertNotEqual(
                target.stat().st_ino,
                first_inode,
            )
            self.assertEqual(
                list(parent.glob(".pqnas.env.tmp.*")),
                [],
            )

    def test_create_only_preserves_existing_file(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = self.private_parent(Path(directory))
            target = parent / "keys.env"

            self.assertTrue(
                self.write(
                    target,
                    "original\n",
                    replace_existing=False,
                )
            )
            self.assertFalse(
                self.write(
                    target,
                    "replacement\n",
                    replace_existing=False,
                )
            )
            self.assertEqual(
                target.read_text(encoding="utf-8"),
                "original\n",
            )

    def test_target_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            parent = self.private_parent(root)
            outside = root / "outside"
            outside.write_text(
                "do not change\n",
                encoding="utf-8",
            )
            target = parent / "pqnas.env"
            target.symlink_to(outside)

            with self.assertRaises(
                secure_files.SecurePrivateFileError
            ):
                self.write(target, "unsafe\n")

            self.assertEqual(
                outside.read_text(encoding="utf-8"),
                "do not change\n",
            )

    def test_parent_symlink_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            real_parent = self.private_parent(root)
            link_parent = root / "linked"
            link_parent.symlink_to(
                real_parent,
                target_is_directory=True,
            )

            with self.assertRaises(
                secure_files.SecurePrivateFileError
            ):
                self.write(
                    link_parent / "pqnas.env",
                    "unsafe\n",
                )

    def test_hardlinked_target_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            parent = self.private_parent(root)
            target = parent / "pqnas.env"
            alias = parent / "alias"

            target.write_text(
                "original\n",
                encoding="utf-8",
            )
            target.chmod(0o600)
            os.link(target, alias)

            with self.assertRaises(
                secure_files.SecurePrivateFileError
            ):
                self.write(target, "unsafe\n")

    def test_group_writable_target_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = self.private_parent(Path(directory))
            target = parent / "pqnas.env"

            target.write_text(
                "original\n",
                encoding="utf-8",
            )
            target.chmod(0o620)

            with self.assertRaises(
                secure_files.SecurePrivateFileError
            ):
                self.write(target, "unsafe\n")

    def test_write_failure_preserves_original_and_cleans_temp(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = self.private_parent(Path(directory))
            target = parent / "pqnas.env"

            self.assertTrue(
                self.write(target, "original\n")
            )

            with mock.patch.object(
                secure_files,
                "_write_all",
                side_effect=secure_files.SecurePrivateFileError(
                    "simulated write failure"
                ),
            ):
                with self.assertRaises(
                    secure_files.SecurePrivateFileError
                ):
                    self.write(target, "replacement\n")

            self.assertEqual(
                target.read_text(encoding="utf-8"),
                "original\n",
            )
            self.assertEqual(
                list(parent.glob(".pqnas.env.tmp.*")),
                [],
            )

    def test_replace_failure_preserves_original_and_cleans_temp(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = self.private_parent(Path(directory))
            target = parent / "pqnas.env"

            self.assertTrue(
                self.write(target, "original\n")
            )

            with mock.patch.object(
                secure_files.os,
                "replace",
                side_effect=OSError(
                    "simulated replace failure"
                ),
            ):
                with self.assertRaises(
                    secure_files.SecurePrivateFileError
                ):
                    self.write(target, "replacement\n")

            self.assertEqual(
                target.read_text(encoding="utf-8"),
                "original\n",
            )
            self.assertEqual(
                list(parent.glob(".pqnas.env.tmp.*")),
                [],
            )

    def test_read_private_file_text(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = self.private_parent(Path(directory))
            target = parent / "keys.env"

            self.assertIsNone(
                secure_files.read_private_file_text(
                    str(target),
                    owner_uid=self.owner_uid,
                    owner_gid=self.owner_gid,
                )
            )

            self.assertTrue(
                self.write(
                    target,
                    "KEY=value\n",
                    replace_existing=False,
                )
            )
            self.assertEqual(
                secure_files.read_private_file_text(
                    str(target),
                    owner_uid=self.owner_uid,
                    owner_gid=self.owner_gid,
                ),
                "KEY=value\n",
            )

    def test_exact_private_env_keys_accepts_valid_file(
        self,
    ) -> None:
        required = {
            "PQNAS_SERVER_PK_B64URL",
            "PQNAS_SERVER_SK_B64URL",
            "PQNAS_COOKIE_KEY_B64URL",
        }
        text = (
            "PQNAS_SERVER_PK_B64URL=public_key_1\n"
            "PQNAS_SERVER_SK_B64URL=secret_key_2\n"
            "PQNAS_COOKIE_KEY_B64URL=cookie_key_3\n"
        )

        secure_files.validate_exact_private_env_keys(
            text,
            required,
        )

    def test_exact_private_env_keys_rejects_bad_shapes(
        self,
    ) -> None:
        required = {
            "KEY_A",
            "KEY_B",
        }
        cases = (
            "KEY_A=value_a\n",
            "KEY_A=value_a\nKEY_A=value_b\nKEY_B=value_b\n",
            "KEY_A=value_a\nKEY_B=value_b\nEXTRA=value_c\n",
            "KEY_A=\nKEY_B=value_b\n",
            "KEY_A=value a\nKEY_B=value_b\n",
            "KEY_A=value_a\n\nKEY_B=value_b\n",
        )

        for text in cases:
            with self.subTest(text=text):
                with self.assertRaises(
                    secure_files.SecurePrivateFileError
                ):
                    secure_files.validate_exact_private_env_keys(
                        text,
                        required,
                    )

    def test_private_env_error_does_not_echo_secret(
        self,
    ) -> None:
        secret = "secret=value-with-equals"

        with self.assertRaises(
            secure_files.SecurePrivateFileError
        ) as caught:
            secure_files.validate_exact_private_env_keys(
                f"KEY={secret}\n",
                {"KEY"},
            )

        self.assertNotIn(
            secret,
            str(caught.exception),
        )

    def test_private_file_exists_rejects_unsafe_metadata(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = self.private_parent(Path(directory))
            target = parent / "keys.env"

            target.write_text(
                "unsafe\n",
                encoding="utf-8",
            )
            target.chmod(0o644)

            with self.assertRaises(
                secure_files.SecurePrivateFileError
            ):
                secure_files.private_file_exists(
                    str(target),
                    owner_uid=self.owner_uid,
                    owner_gid=self.owner_gid,
                )


if __name__ == "__main__":
    unittest.main()
