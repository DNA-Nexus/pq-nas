#!/usr/bin/env python3

from __future__ import annotations

import json
import os
import re
import stat
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(
    0,
    str(ROOT / "tools" / "installer"),
)

import pqnas_director_projection_state as director_state  # noqa: E402
from pqnas_cluster_config import ClusterInstallConfig  # noqa: E402


class DirectorProjectionStateTests(unittest.TestCase):
    def setUp(self) -> None:
        self.uid = os.geteuid()
        self.gid = os.getegid()

    def storage_root(self, base: Path) -> Path:
        root = base / "storage"
        root.mkdir(mode=0o700)
        return root

    def director(self) -> ClusterInstallConfig:
        return ClusterInstallConfig(role="director")

    def test_schema_version_matches_runtime_header(self) -> None:
        header = (
            ROOT
            / "server"
            / "src"
            / "cluster"
            / "storage_user_projection_sync_state_store.h"
        ).read_text(encoding="utf-8")

        match = re.search(
            r"kStorageUserProjectionSyncStateSchemaVersion\s*=\s*(\d+)",
            header,
        )
        self.assertIsNotNone(match)
        self.assertEqual(
            director_state
            .DIRECTOR_PROJECTION_SYNC_STATE_SCHEMA_VERSION,
            int(match.group(1)),
        )

    def test_non_director_does_not_create_state(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.storage_root(Path(directory))

            action = (
                director_state
                .ensure_director_projection_sync_state(
                    str(root),
                    ClusterInstallConfig(role="standalone"),
                    bootstrap_approved=True,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )

            self.assertEqual(action, "not-applicable")
            self.assertFalse(
                Path(
                    director_state
                    .director_projection_sync_state_path(
                        str(root)
                    )
                ).exists()
            )
            self.assertFalse(
                Path(
                    director_state
                    .director_projection_transition_journal_path(
                        str(root)
                    )
                ).exists()
            )

    def test_missing_director_state_requires_approval(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.storage_root(Path(directory))

            with self.assertRaises(
                director_state.DirectorProjectionStateError
            ):
                (
                    director_state
                    .ensure_director_projection_sync_state(
                        str(root),
                        self.director(),
                        bootstrap_approved=False,
                        owner_uid=self.uid,
                        owner_gid=self.gid,
                    )
                )

            self.assertFalse(
                Path(
                    director_state
                    .director_projection_sync_state_path(
                        str(root)
                    )
                ).exists()
            )
            self.assertFalse(
                Path(
                    director_state
                    .director_projection_transition_journal_path(
                        str(root)
                    )
                ).exists()
            )

    def test_approved_bootstrap_creates_empty_private_state(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.storage_root(Path(directory))

            action = (
                director_state
                .ensure_director_projection_sync_state(
                    str(root),
                    self.director(),
                    bootstrap_approved=True,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )

            self.assertEqual(action, "created")

            path = Path(
                director_state
                .director_projection_sync_state_path(
                    str(root)
                )
            )
            payload = json.loads(
                path.read_text(encoding="utf-8")
            )

            self.assertEqual(
                payload,
                {
                    "version": 1,
                    "states": [],
                },
            )

            metadata = path.stat()
            self.assertEqual(
                stat.S_IMODE(metadata.st_mode),
                0o600,
            )
            self.assertEqual(metadata.st_uid, self.uid)
            self.assertEqual(metadata.st_gid, self.gid)

            journal_path = Path(
                director_state
                .director_projection_transition_journal_path(
                    str(root)
                )
            )
            journal_payload = json.loads(
                journal_path.read_text(
                    encoding="utf-8"
                )
            )

            self.assertEqual(
                journal_payload,
                {
                    "version": 1,
                    "states": [],
                },
            )

            journal_metadata = journal_path.stat()
            self.assertEqual(
                stat.S_IMODE(
                    journal_metadata.st_mode
                ),
                0o600,
            )
            self.assertEqual(
                journal_metadata.st_uid,
                self.uid,
            )
            self.assertEqual(
                journal_metadata.st_gid,
                self.gid,
            )

    def test_existing_state_is_never_reset(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.storage_root(Path(directory))
            config = root / "config"
            config.mkdir(mode=0o700)

            path = (
                config
                / director_state
                .DIRECTOR_PROJECTION_SYNC_STATE_FILENAME
            )
            original = (
                '{\n'
                '  "version": 1,\n'
                '  "states": [\n'
                '    {\n'
                '      "fingerprint": "'
                + ("a" * 64)
                + '",\n'
                '      "home_node": "node-a",\n'
                '      "revision": 17\n'
                '    }\n'
                '  ]\n'
                '}\n'
            )
            path.write_text(
                original,
                encoding="utf-8",
            )
            path.chmod(0o600)
            inode = path.stat().st_ino

            journal_path = (
                config
                / director_state
                .DIRECTOR_PROJECTION_TRANSITION_JOURNAL_FILENAME
            )
            journal_original = (
                '{\n'
                '  "version": 1,\n'
                '  "states": []\n'
                '}\n'
            )
            journal_path.write_text(
                journal_original,
                encoding="utf-8",
            )
            journal_path.chmod(0o600)
            journal_inode = (
                journal_path.stat().st_ino
            )

            action = (
                director_state
                .ensure_director_projection_sync_state(
                    str(root),
                    self.director(),
                    bootstrap_approved=False,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )

            self.assertEqual(action, "preserved")
            self.assertEqual(
                path.read_text(encoding="utf-8"),
                original,
            )
            self.assertEqual(path.stat().st_ino, inode)
            self.assertEqual(
                journal_path.read_text(
                    encoding="utf-8"
                ),
                journal_original,
            )
            self.assertEqual(
                journal_path.stat().st_ino,
                journal_inode,
            )

    def test_existing_confirmed_state_missing_journal_requires_approval(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.storage_root(
                Path(directory)
            )
            config = root / "config"
            config.mkdir(mode=0o700)

            confirmed_path = (
                config
                / director_state
                .DIRECTOR_PROJECTION_SYNC_STATE_FILENAME
            )
            confirmed_original = (
                '{\n'
                '  "version": 1,\n'
                '  "states": []\n'
                '}\n'
            )
            confirmed_path.write_text(
                confirmed_original,
                encoding="utf-8",
            )
            confirmed_path.chmod(0o600)
            confirmed_inode = (
                confirmed_path.stat().st_ino
            )

            self.assertTrue(
                director_state
                .director_projection_bootstrap_confirmation_required(
                    str(root),
                    "existing",
                    self.director(),
                )
            )

            with self.assertRaises(
                director_state.DirectorProjectionStateError
            ):
                (
                    director_state
                    .ensure_director_projection_sync_state(
                        str(root),
                        self.director(),
                        bootstrap_approved=False,
                        owner_uid=self.uid,
                        owner_gid=self.gid,
                    )
                )

            journal_path = Path(
                director_state
                .director_projection_transition_journal_path(
                    str(root)
                )
            )

            self.assertFalse(
                journal_path.exists()
            )
            self.assertEqual(
                confirmed_path.stat().st_ino,
                confirmed_inode,
            )

            action = (
                director_state
                .ensure_director_projection_sync_state(
                    str(root),
                    self.director(),
                    bootstrap_approved=True,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )

            self.assertEqual(
                action,
                "created",
            )
            self.assertTrue(
                journal_path.exists()
            )
            self.assertEqual(
                confirmed_path.stat().st_ino,
                confirmed_inode,
            )
            self.assertEqual(
                confirmed_path.read_text(
                    encoding="utf-8"
                ),
                confirmed_original,
            )

            self.assertFalse(
                director_state
                .binary_rollback_restart_allowed(
                    "existing",
                    action,
                )
            )

            self.assertFalse(
                director_state
                .director_projection_bootstrap_confirmation_required(
                    str(root),
                    "existing",
                    self.director(),
                )
            )

    def test_existing_preserved_state_allows_binary_rollback_restart(
        self,
    ) -> None:
        self.assertTrue(
            director_state.binary_rollback_restart_allowed(
                "existing",
                "preserved",
            )
        )

    def test_existing_non_director_allows_binary_rollback_restart(
        self,
    ) -> None:
        self.assertTrue(
            director_state.binary_rollback_restart_allowed(
                "existing",
                "not-applicable",
            )
        )

    def test_new_director_state_blocks_binary_rollback_restart(
        self,
    ) -> None:
        self.assertFalse(
            director_state.binary_rollback_restart_allowed(
                "existing",
                "created",
            )
        )

    def test_disk_mode_blocks_binary_rollback_restart(
        self,
    ) -> None:
        for action in (
            "not-applicable",
            "preserved",
            "created",
        ):
            with self.subTest(action=action):
                self.assertFalse(
                    director_state.binary_rollback_restart_allowed(
                        "disk",
                        action,
                    )
                )

    def test_invalid_rollback_policy_inputs_fail_closed(
        self,
    ) -> None:
        with self.assertRaises(
            director_state.DirectorProjectionStateError
        ):
            director_state.binary_rollback_restart_allowed(
                "unknown",
                "preserved",
            )

        with self.assertRaises(
            director_state.DirectorProjectionStateError
        ):
            director_state.binary_rollback_restart_allowed(
                "existing",
                "unknown",
            )

    def test_disk_mode_always_requires_director_bootstrap_confirmation(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.storage_root(Path(directory))

            self.assertTrue(
                director_state
                .director_projection_bootstrap_confirmation_required(
                    str(root),
                    "disk",
                    self.director(),
                )
            )

    def test_existing_mode_requires_confirmation_only_when_missing(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = self.storage_root(Path(directory))

            self.assertTrue(
                director_state
                .director_projection_bootstrap_confirmation_required(
                    str(root),
                    "existing",
                    self.director(),
                )
            )

            (
                director_state
                .ensure_director_projection_sync_state(
                    str(root),
                    self.director(),
                    bootstrap_approved=True,
                    owner_uid=self.uid,
                    owner_gid=self.gid,
                )
            )

            self.assertFalse(
                director_state
                .director_projection_bootstrap_confirmation_required(
                    str(root),
                    "existing",
                    self.director(),
                )
            )


if __name__ == "__main__":
    unittest.main()
