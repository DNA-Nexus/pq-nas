#!/usr/bin/env python3
from __future__ import annotations

import base64
import importlib.util
import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
COLLECTOR = (
    ROOT
    / "tools"
    / "dev"
    / "collect_installer_cluster_smoke_evidence.py"
)

SPEC = importlib.util.spec_from_file_location(
    "collect_installer_cluster_smoke_evidence",
    COLLECTOR,
)
assert SPEC is not None
assert SPEC.loader is not None
collector = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(collector)


class InstallerClusterSmokeEvidenceTests(unittest.TestCase):
    def valid_token(self, seed: int = 0) -> bytes:
        raw = bytes(
            (index + seed) % 256
            for index in range(32)
        )
        return base64.urlsafe_b64encode(
            raw
        ).rstrip(b"=")

    def test_env_summary_never_returns_secret_values(self) -> None:
        secret = "A" * 43
        contents = (
            "PQNAS_SERVER_ROLE=node\n"
            "PQNAS_NODE_ID=cellnode1\n"
            "PQNAS_DIRECTOR_URL=https://director.internal\n"
            "PQNAS_CLUSTER_CREDENTIAL_FILE="
            "/var/lib/pqnas/cluster/node-credential\n"
            "PQNAS_NODE_ENROLLMENT_ID=enrollment_1\n"
            "PQNAS_NODE_ENROLLMENT_GENERATION=1\n"
            "PQNAS_NODE_API_BASE_URL=https://cellnode1.internal\n"
            "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE="
            "/var/lib/pqnas/cluster/bootstrap/enrollment.token\n"
            f"PQNAS_NODE_BOOTSTRAP_TOKEN={secret}\n"
            "PQNAS_COOKIE_KEY=do-not-return-this\n"
        ).encode("utf-8")

        summary = collector.summarize_env(
            contents
        )
        serialized = json.dumps(summary)

        self.assertIn(
            "PQNAS_NODE_BOOTSTRAP_TOKEN",
            summary["forbidden_keys"],
        )
        self.assertIn(
            "PQNAS_COOKIE_KEY",
            summary["keys"],
        )
        self.assertNotIn(
            secret,
            serialized,
        )
        self.assertNotIn(
            "do-not-return-this",
            serialized,
        )
        self.assertEqual(
            summary["cluster"]["PQNAS_SERVER_ROLE"],
            "node",
        )

    def test_private_reader_rejects_symlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outside = root / "outside"
            outside.write_text(
                "safe\n",
                encoding="utf-8",
            )
            outside.chmod(0o600)
            link = root / "link"
            link.symlink_to(
                outside
            )

            with self.assertRaises(
                collector.CollectorError
            ) as context:
                collector.read_private_file_bounded(
                    link,
                    1024,
                    owner_uid=os.geteuid(),
                    owner_gid=os.getegid(),
                    mode=0o600,
                )

            self.assertEqual(
                context.exception.reason,
                "symlink_rejected",
            )

    def test_private_reader_rejects_hardlink(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            target = root / "target"
            alias = root / "alias"
            target.write_text(
                "safe\n",
                encoding="utf-8",
            )
            target.chmod(0o600)
            os.link(
                target,
                alias,
            )

            with self.assertRaises(
                collector.CollectorError
            ) as context:
                collector.read_private_file_bounded(
                    target,
                    1024,
                    owner_uid=os.geteuid(),
                    owner_gid=os.getegid(),
                    mode=0o600,
                )

            self.assertEqual(
                context.exception.reason,
                "hardlink_rejected",
            )

    def test_private_reader_checks_opened_file_mode(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "pqnas.env"
            target.write_text(
                "PQNAS_SERVER_ROLE=standalone\n",
                encoding="utf-8",
            )
            target.chmod(0o644)

            with self.assertRaises(
                collector.CollectorError
            ) as context:
                collector.read_private_file_bounded(
                    target,
                    1024,
                    owner_uid=os.geteuid(),
                    owner_gid=os.getegid(),
                    mode=0o600,
                )

            self.assertEqual(
                context.exception.reason,
                "file_metadata_unsafe",
            )

    def test_private_reader_returns_mutable_buffer(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "pqnas.env"
            target.write_text(
                "PQNAS_SERVER_ROLE=standalone\n",
                encoding="utf-8",
            )
            target.chmod(0o600)

            contents = collector.read_private_file_bounded(
                target,
                1024,
                owner_uid=os.geteuid(),
                owner_gid=os.getegid(),
                mode=0o600,
            )
            self.assertIsInstance(contents, bytearray)

            collector.wipe_secret(contents)
            self.assertEqual(
                contents,
                bytearray(len(contents)),
            )

    def test_secret_input_requires_canonical_base64url(self) -> None:
        valid = self.valid_token()
        buffer = collector.validate_secret_input(
            valid + b"\n"
        )
        self.assertIsInstance(
            buffer,
            bytearray,
        )
        self.assertEqual(
            bytes(buffer),
            valid,
        )

        alphabet = (
            b"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            b"abcdefghijklmnopqrstuvwxyz"
            b"0123456789-_"
        )
        index = alphabet.index(
            valid[-1:]
        )
        replacement = alphabet[
            (index & 0b111100) | 0b000001
        ]
        noncanonical = (
            valid[:-1]
            + bytes([replacement])
        )

        with self.assertRaises(
            collector.CollectorError
        ) as context:
            collector.validate_secret_input(
                noncanonical
            )

        self.assertEqual(
            context.exception.reason,
            "secret_input_noncanonical",
        )

    def test_secret_result_never_contains_secret(self) -> None:
        secret = self.valid_token()
        item = collector.secret_result(
            "secret.journal",
            True,
            lines_requested=500,
        )
        serialized = json.dumps(item)

        self.assertEqual(
            item["reason"],
            "secret_found",
        )
        self.assertNotIn(
            secret.decode("ascii"),
            serialized,
        )

    def test_invalid_mutable_secret_input_is_wiped(self) -> None:
        invalid = bytearray(b"x" * 42)

        with self.assertRaises(
            collector.CollectorError
        ):
            collector.validate_secret_input(
                invalid
            )

        self.assertEqual(
            invalid,
            bytearray(len(invalid)),
        )

    def test_filesystem_checks_use_configured_node_paths(self) -> None:
        cluster = {
            "PQNAS_SERVER_ROLE": "node",
            "PQNAS_NODE_ID": "cellnode1",
            "PQNAS_DIRECTOR_URL": "https://director.internal",
            "PQNAS_CLUSTER_CREDENTIAL_FILE": (
                "/var/lib/pqnas/custom/node.credential"
            ),
            "PQNAS_NODE_ENROLLMENT_ID": "enrollment_1",
            "PQNAS_NODE_ENROLLMENT_GENERATION": 1,
            "PQNAS_NODE_API_BASE_URL": (
                "https://cellnode1.internal"
            ),
            "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE": (
                "/var/lib/pqnas/custom/bootstrap.token"
            ),
        }

        checks = collector.filesystem_checks(
            "node-enrolled",
            cluster,
        )
        by_id = {
            item["id"]: item
            for item in checks
        }

        self.assertEqual(
            by_id["path.node_credential"]["details"]["path"],
            cluster["PQNAS_CLUSTER_CREDENTIAL_FILE"],
        )
        self.assertEqual(
            by_id["path.bootstrap_token"]["details"]["path"],
            cluster["PQNAS_NODE_BOOTSTRAP_TOKEN_FILE"],
        )

    def test_journal_failure_is_not_reported_as_clean(self) -> None:
        secret = bytearray(self.valid_token())
        journal = bytearray(b"untrusted journal output")

        with mock.patch.object(
            collector,
            "run_bounded",
            return_value=(1, journal),
        ), mock.patch.object(
            collector,
            "process_secret_checks",
            return_value=[],
        ):
            checks = collector.leak_checks(
                secret,
                None,
                skip_systemd=False,
            )

        journal_check = next(
            item
            for item in checks
            if item["id"] == "secret.journal"
        )
        self.assertEqual(
            journal_check["status"],
            "warn",
        )
        self.assertEqual(
            journal_check["reason"],
            "journal_query_failed",
        )
        self.assertEqual(
            journal,
            bytearray(len(journal)),
        )

    def test_process_scan_marks_unreadable_entries_incomplete(
        self,
    ) -> None:
        secret = bytearray(self.valid_token())

        with mock.patch.object(
            Path,
            "iterdir",
            return_value=iter([Path("/proc/123")]),
        ), mock.patch.object(
            collector,
            "read_proc_file_bounded",
            side_effect=collector.CollectorError(
                "file_open_failed"
            ),
        ):
            checks = collector.process_secret_checks(
                secret
            )

        self.assertEqual(
            {item["status"] for item in checks},
            {"warn"},
        )
        self.assertEqual(
            {item["reason"] for item in checks},
            {"secret_scan_incomplete"},
        )

    def test_wipe_secret_overwrites_buffer(self) -> None:
        secret = bytearray(
            self.valid_token()
        )
        collector.wipe_secret(
            secret
        )
        self.assertEqual(
            secret,
            bytearray(len(secret)),
        )

    def test_overall_status_prioritizes_failures(self) -> None:
        self.assertEqual(
            collector.overall([
                collector.check("a", "pass", "ok"),
                collector.check("b", "warn", "warning"),
            ]),
            "warn",
        )
        self.assertEqual(
            collector.overall([
                collector.check("a", "warn", "warning"),
                collector.check("b", "fail", "failed"),
            ]),
            "fail",
        )


if __name__ == "__main__":
    unittest.main()
