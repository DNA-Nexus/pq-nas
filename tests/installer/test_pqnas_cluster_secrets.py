#!/usr/bin/env python3
from __future__ import annotations

import base64
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

import pqnas_cluster_secrets as cluster_secrets  # noqa: E402
from pqnas_cluster_config import ClusterInstallConfig  # noqa: E402


class ClusterSecretTests(unittest.TestCase):
    def setUp(self) -> None:
        self.uid = os.geteuid()
        self.gid = os.getegid()

    def valid_token(self, seed: int = 0) -> str:
        raw = bytes(
            (index + seed) % 256
            for index in range(32)
        )
        return base64.urlsafe_b64encode(
            raw
        ).decode("ascii").rstrip("=")

    def prepare_parent(self, parent: Path) -> None:
        parent.mkdir(
            parents=True,
            mode=0o750,
        )
        parent.chmod(
            0o750
        )

    def node_config(
        self,
        root: Path,
        *,
        node_id: str = "cellnode1",
    ) -> ClusterInstallConfig:
        return ClusterInstallConfig(
            role="node",
            node_id=node_id,
            director_url="https://director.internal",
            cluster_credential_file=str(
                root / "node-credential"
            ),
            enrollment_id="enrollment_1",
            enrollment_generation=1,
            node_api_base_url=(
                "https://cellnode1.internal"
            ),
            bootstrap_token_file=str(
                root / "enrollment.token"
            ),
        )

    def test_valid_token_is_returned_as_mutable_buffer(
        self,
    ) -> None:
        token = self.valid_token()

        buffer = (
            cluster_secrets
            .validate_node_bootstrap_token(token)
        )

        self.assertIsInstance(
            buffer,
            bytearray,
        )
        self.assertEqual(
            buffer.decode("ascii"),
            token,
        )

    def test_invalid_tokens_are_rejected(self) -> None:
        valid = self.valid_token()

        cases = (
            "",
            valid + "=",
            " " + valid[1:],
            valid[:-1],
            valid[:-1] + "+",
        )

        for value in cases:
            with self.subTest(value=value):
                with self.assertRaises(
                    cluster_secrets.ClusterSecretError
                ):
                    cluster_secrets.validate_node_bootstrap_token(
                        value
                    )

    def test_noncanonical_token_is_rejected(self) -> None:
        valid = self.valid_token()
        alphabet = (
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789-_"
        )
        index = alphabet.index(valid[-1])

        # A 32-byte base64url value has four data bits and two zero padding
        # bits in its final character. Preserve the data bits while forcing
        # a non-zero padding-bit pattern.
        replacement = alphabet[
            (index & 0b111100) | 0b000001
        ]
        candidate = valid[:-1] + replacement

        with self.assertRaises(
            cluster_secrets.ClusterSecretError
        ):
            cluster_secrets.validate_node_bootstrap_token(
                candidate
            )

    def test_wipe_secret_buffer(self) -> None:
        buffer = bytearray(
            b"secret"
        )

        cluster_secrets.wipe_secret_buffer(
            buffer
        )

        self.assertEqual(
            buffer,
            bytearray(len(buffer)),
        )

    def test_atomic_write_and_replace(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            self.prepare_parent(parent)
            target = parent / "enrollment.token"

            first = (
                cluster_secrets
                .validate_node_bootstrap_token(
                    self.valid_token(0)
                )
            )
            cluster_secrets.atomic_write_node_bootstrap_token(
                str(target),
                first,
                owner_uid=self.uid,
                owner_gid=self.gid,
            )
            first_inode = target.stat().st_ino

            self.assertEqual(
                target.read_text(encoding="ascii"),
                self.valid_token(0) + "\n",
            )
            self.assertEqual(
                stat.S_IMODE(target.stat().st_mode),
                0o600,
            )

            second = (
                cluster_secrets
                .validate_node_bootstrap_token(
                    self.valid_token(1)
                )
            )
            cluster_secrets.atomic_write_node_bootstrap_token(
                str(target),
                second,
                owner_uid=self.uid,
                owner_gid=self.gid,
            )

            self.assertNotEqual(
                target.stat().st_ino,
                first_inode,
            )
            self.assertEqual(
                target.read_text(encoding="ascii"),
                self.valid_token(1) + "\n",
            )

    def test_symlink_target_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            parent = base / "private"
            self.prepare_parent(parent)
            outside = base / "outside"
            outside.write_text(
                "keep\n",
                encoding="utf-8",
            )
            target = parent / "enrollment.token"
            target.symlink_to(
                outside
            )
            token = (
                cluster_secrets
                .validate_node_bootstrap_token(
                    self.valid_token()
                )
            )

            with self.assertRaises(
                cluster_secrets.ClusterSecretError
            ):
                cluster_secrets.atomic_write_node_bootstrap_token(
                    str(target),
                    token,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

            self.assertEqual(
                outside.read_text(encoding="utf-8"),
                "keep\n",
            )

    def test_hardlinked_target_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            self.prepare_parent(parent)
            target = parent / "enrollment.token"
            alias = parent / "alias"
            target.write_text(
                self.valid_token() + "\n",
                encoding="ascii",
            )
            target.chmod(
                0o600
            )
            os.link(
                target,
                alias,
            )
            token = (
                cluster_secrets
                .validate_node_bootstrap_token(
                    self.valid_token(1)
                )
            )

            with self.assertRaises(
                cluster_secrets.ClusterSecretError
            ):
                cluster_secrets.atomic_write_node_bootstrap_token(
                    str(target),
                    token,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

    def test_safe_removal(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            self.prepare_parent(parent)
            target = parent / "enrollment.token"
            token = (
                cluster_secrets
                .validate_node_bootstrap_token(
                    self.valid_token()
                )
            )
            cluster_secrets.atomic_write_node_bootstrap_token(
                str(target),
                token,
                owner_uid=self.uid,
                owner_gid=self.gid,
            )

            self.assertTrue(
                cluster_secrets.remove_node_bootstrap_token(
                    str(target),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )
            self.assertFalse(
                target.exists()
            )
            self.assertFalse(
                cluster_secrets.remove_node_bootstrap_token(
                    str(target),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )

    def test_apply_requires_token_for_changed_node_config(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            self.prepare_parent(parent)
            selected = self.node_config(
                parent
            )

            with mock.patch.object(
                cluster_secrets,
                "service_owner_ids",
                return_value=(
                    self.uid,
                    self.gid,
                ),
            ):
                with self.assertRaises(
                    cluster_secrets.ClusterSecretError
                ):
                    cluster_secrets.apply_cluster_bootstrap_secret(
                        ClusterInstallConfig(),
                        selected,
                        None,
                    )

    def test_default_cluster_directories_are_owner_only(
        self,
    ) -> None:
        selected = ClusterInstallConfig(
            role="node",
            node_id="cellnode1",
            director_url="https://director.internal",
            cluster_credential_file=(
                cluster_secrets.DEFAULT_NODE_CREDENTIAL_FILE
            ),
            enrollment_id="enrollment_1",
            enrollment_generation=1,
            node_api_base_url="https://cellnode1.internal",
            bootstrap_token_file=(
                cluster_secrets.DEFAULT_NODE_BOOTSTRAP_TOKEN_FILE
            ),
        )
        token = (
            cluster_secrets
            .validate_node_bootstrap_token(
                self.valid_token()
            )
        )

        with mock.patch.object(
            cluster_secrets,
            "service_owner_ids",
            return_value=(self.uid, self.gid),
        ), mock.patch.object(
            cluster_secrets,
            "prepare_service_state_directory",
        ) as prepare_directory, mock.patch.object(
            cluster_secrets,
            "atomic_write_node_bootstrap_token",
        ):
            action = (
                cluster_secrets
                .apply_cluster_bootstrap_secret(
                    ClusterInstallConfig(),
                    selected,
                    token,
                )
            )

        self.assertEqual(action, "written")
        self.assertEqual(
            prepare_directory.call_args_list,
            [
                mock.call(
                    "/var/lib/pqnas/cluster",
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                    directory_mode=0o700,
                ),
                mock.call(
                    "/var/lib/pqnas/cluster/bootstrap",
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                    directory_mode=0o700,
                ),
            ],
        )
        self.assertEqual(
            token,
            bytearray(len(token)),
        )

    def test_apply_writes_and_wipes_token(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            self.prepare_parent(parent)
            selected = self.node_config(
                parent
            )
            token = (
                cluster_secrets
                .validate_node_bootstrap_token(
                    self.valid_token()
                )
            )

            with mock.patch.object(
                cluster_secrets,
                "service_owner_ids",
                return_value=(
                    self.uid,
                    self.gid,
                ),
            ):
                action = (
                    cluster_secrets
                    .apply_cluster_bootstrap_secret(
                        ClusterInstallConfig(),
                        selected,
                        token,
                    )
                )

            self.assertEqual(
                action,
                "written",
            )
            self.assertEqual(
                token,
                bytearray(len(token)),
            )
            self.assertEqual(
                Path(
                    selected.bootstrap_token_file
                ).read_text(encoding="ascii"),
                self.valid_token() + "\n",
            )

    def test_writer_rejects_noncanonical_buffer(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            self.prepare_parent(parent)
            target = parent / "enrollment.token"

            valid = self.valid_token()
            alphabet = (
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz"
                "0123456789-_"
            )
            index = alphabet.index(valid[-1])
            replacement = alphabet[
                (index & 0b111100) | 0b000001
            ]
            candidate = bytearray(
                (valid[:-1] + replacement).encode("ascii")
            )

            with self.assertRaises(
                cluster_secrets.ClusterSecretError
            ):
                cluster_secrets.atomic_write_node_bootstrap_token(
                    str(target),
                    candidate,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

            self.assertFalse(
                target.exists()
            )

    def test_remove_rejects_symlink_target(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            base = Path(directory)
            parent = base / "private"
            self.prepare_parent(parent)
            outside = base / "outside"
            outside.write_text(
                self.valid_token() + "\n",
                encoding="ascii",
            )
            outside.chmod(
                0o600
            )
            target = parent / "enrollment.token"
            target.symlink_to(
                outside
            )

            with self.assertRaises(
                cluster_secrets.ClusterSecretError
            ):
                cluster_secrets.remove_node_bootstrap_token(
                    str(target),
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )

            self.assertTrue(
                outside.exists()
            )

    def test_apply_wipes_token_on_write_failure(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            self.prepare_parent(parent)
            selected = self.node_config(
                parent
            )
            token = (
                cluster_secrets
                .validate_node_bootstrap_token(
                    self.valid_token()
                )
            )

            with mock.patch.object(
                cluster_secrets,
                "service_owner_ids",
                return_value=(
                    self.uid,
                    self.gid,
                ),
            ), mock.patch.object(
                cluster_secrets,
                "atomic_write_node_bootstrap_token",
                side_effect=cluster_secrets.ClusterSecretError(
                    "injected write failure"
                ),
            ):
                with self.assertRaises(
                    cluster_secrets.ClusterSecretError
                ):
                    cluster_secrets.apply_cluster_bootstrap_secret(
                        ClusterInstallConfig(),
                        selected,
                        token,
                    )

            self.assertEqual(
                token,
                bytearray(len(token)),
            )


if __name__ == "__main__":
    unittest.main()
