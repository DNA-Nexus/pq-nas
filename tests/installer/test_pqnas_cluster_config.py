#!/usr/bin/env python3
from __future__ import annotations

import os
import sys
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(
    0,
    str(ROOT / "tools" / "installer"),
)

from pqnas_cluster_config import (  # noqa: E402
    MAX_GENERATION,
    ClusterConfigError,
    ClusterInstallConfig,
    is_legacy_director_tls_pin_upgrade,
    load_existing_cluster_config,
    parse_cluster_config_from_env_text,
    render_cluster_env_lines,
    validate_cluster_config,
)
from pqnas_secure_files import (  # noqa: E402
    atomic_write_private_file,
)


class ClusterConfigTests(unittest.TestCase):
    def setUp(self) -> None:
        self.owner_uid = os.geteuid()
        self.owner_gid = os.getegid()

    def node_config(self) -> ClusterInstallConfig:
        return ClusterInstallConfig(
            role="node",
            node_id="cellnode1",
            director_url=(
                "https://director.example.com/"
            ),
            director_tls_spki_sha256_pin=(
                "sha256/"
                "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
            ),
            cluster_credential_file=(
                "/var/lib/pqnas/cluster/"
                "node-credential"
            ),
            enrollment_id="ENR_0123456789",
            enrollment_generation=7,
            node_api_base_url=(
                "https://cellnode1.example.com/"
            ),
            bootstrap_token_file=(
                "/var/lib/pqnas/cluster/bootstrap/"
                "enrollment.token"
            ),
        )

    def test_missing_env_is_legacy_standalone(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            parent.mkdir(mode=0o700)
            parent.chmod(0o700)

            self.assertEqual(
                load_existing_cluster_config(
                    str(parent / "missing.env"),
                    owner_uid=self.owner_uid,
                    owner_gid=self.owner_gid,
                ),
                ClusterInstallConfig(),
            )

    def test_missing_parent_is_legacy_standalone(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = (
                Path(directory)
                / "missing-private-directory"
                / "pqnas.env"
            )

            self.assertEqual(
                load_existing_cluster_config(
                    str(path),
                    owner_uid=self.owner_uid,
                    owner_gid=self.owner_gid,
                ),
                ClusterInstallConfig(),
            )

    def test_legacy_env_without_cluster_keys_is_standalone(
        self,
    ) -> None:
        parsed = parse_cluster_config_from_env_text(
            "PQNAS_CONFIG=/etc/pqnas\n"
            "PQNAS_DATA_ROOT=/srv/pqnas/data\n"
        )

        self.assertEqual(
            parsed,
            ClusterInstallConfig(),
        )

    def test_standalone_round_trip(self) -> None:
        config = ClusterInstallConfig()
        text = "\n".join(
            render_cluster_env_lines(config)
        ) + "\n"

        self.assertEqual(
            parse_cluster_config_from_env_text(
                text
            ),
            config,
        )
        self.assertIn(
            "PQNAS_SERVER_ROLE=standalone",
            text,
        )

    def test_director_round_trip_without_credential(
        self,
    ) -> None:
        config = ClusterInstallConfig(
            role="director"
        )
        text = "\n".join(
            render_cluster_env_lines(config)
        ) + "\n"

        self.assertEqual(
            parse_cluster_config_from_env_text(
                text
            ),
            config,
        )
        self.assertNotIn(
            "PQNAS_CLUSTER_CREDENTIAL_FILE",
            text,
        )

    def test_director_rejects_node_settings(self) -> None:
        with self.assertRaises(
            ClusterConfigError
        ):
            validate_cluster_config(
                ClusterInstallConfig(
                    role="director",
                    cluster_credential_file=(
                        "/var/lib/pqnas/cluster/"
                        "legacy-credential"
                    ),
                )
            )

    def test_node_round_trip_normalizes_urls(self) -> None:
        normalized = validate_cluster_config(
            self.node_config()
        )

        self.assertEqual(
            normalized.director_url,
            "https://director.example.com",
        )
        self.assertEqual(
            normalized.node_api_base_url,
            "https://cellnode1.example.com",
        )

        text = "\n".join(
            render_cluster_env_lines(normalized)
        ) + "\n"

        self.assertEqual(
            parse_cluster_config_from_env_text(
                text
            ),
            normalized,
        )

        expected_keys = (
            "PQNAS_SERVER_ROLE=node",
            "PQNAS_NODE_ID=cellnode1",
            "PQNAS_DIRECTOR_URL=",
            "PQNAS_DIRECTOR_TLS_SPKI_SHA256_PIN=",
            "PQNAS_CLUSTER_CREDENTIAL_FILE=",
            "PQNAS_NODE_ENROLLMENT_ID=",
            "PQNAS_NODE_ENROLLMENT_GENERATION=7",
            "PQNAS_NODE_API_BASE_URL=",
            "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE=",
        )
        for expected in expected_keys:
            self.assertIn(
                expected,
                text,
            )

    def test_node_config_has_no_token_value_field(
        self,
    ) -> None:
        fields = ClusterInstallConfig.__dataclass_fields__

        self.assertNotIn(
            "bootstrap_token",
            fields,
        )
        self.assertIn(
            "bootstrap_token_file",
            fields,
        )

    def test_node_requires_canonical_director_tls_pin(
        self,
    ) -> None:
        invalid_pins = (
            "",
            " sha256/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
            "sha256/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA= ",
            "sha256/short=",
            (
                "sha256/" +
                ("A" * 42) +
                "B="
            ),
            "SHA256/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
        )

        for value in invalid_pins:
            with self.subTest(pin=value):
                with self.assertRaises(
                    ClusterConfigError
                ):
                    validate_cluster_config(
                        replace(
                            self.node_config(),
                            director_tls_spki_sha256_pin=value,
                        )
                    )

    def test_legacy_node_env_without_director_pin_is_read_compatible(
        self,
    ) -> None:
        normalized = validate_cluster_config(
            self.node_config()
        )
        text = "\n".join(
            render_cluster_env_lines(normalized)
        ) + "\n"

        legacy_text = "\n".join(
            line
            for line in text.splitlines()
            if not line.startswith(
                "PQNAS_DIRECTOR_TLS_SPKI_SHA256_PIN="
            )
        ) + "\n"

        parsed = parse_cluster_config_from_env_text(
            legacy_text
        )

        self.assertEqual(
            parsed.role,
            "node",
        )
        self.assertEqual(
            parsed.director_tls_spki_sha256_pin,
            "",
        )

        # Security: compatibility is read-only. Rewriting a node config must
        # provision the new out-of-band Director trust anchor.
        with self.assertRaises(
            ClusterConfigError
        ):
            render_cluster_env_lines(parsed)

    def test_explicit_blank_director_pin_is_not_legacy(
        self,
    ) -> None:
        normalized = validate_cluster_config(
            self.node_config()
        )
        text = "\n".join(
            render_cluster_env_lines(normalized)
        ) + "\n"
        text = text.replace(
            (
                "PQNAS_DIRECTOR_TLS_SPKI_SHA256_PIN="
                f"{normalized.director_tls_spki_sha256_pin}"
            ),
            "PQNAS_DIRECTOR_TLS_SPKI_SHA256_PIN=",
        )

        with self.assertRaises(
            ClusterConfigError
        ):
            parse_cluster_config_from_env_text(
                text
            )

    def test_first_legacy_director_pin_addition_is_not_reenrollment(
        self,
    ) -> None:
        selected = validate_cluster_config(
            self.node_config()
        )
        original = replace(
            selected,
            director_tls_spki_sha256_pin="",
        )

        self.assertTrue(
            is_legacy_director_tls_pin_upgrade(
                original,
                selected,
            )
        )

        self.assertFalse(
            is_legacy_director_tls_pin_upgrade(
                original,
                replace(
                    selected,
                    enrollment_generation=(
                        selected.enrollment_generation + 1
                    ),
                ),
            )
        )

        self.assertFalse(
            is_legacy_director_tls_pin_upgrade(
                selected,
                replace(
                    selected,
                    director_tls_spki_sha256_pin=(
                        "sha256/"
                        "AQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA="
                    ),
                ),
            )
        )

    def test_node_rejects_invalid_identifiers(self) -> None:
        invalid_node_ids = (
            "",
            "-node",
            "node space",
            "n" * 65,
        )
        invalid_enrollment_ids = (
            "",
            "enrollment.id",
            "enrollment space",
            "e" * 129,
        )

        for value in invalid_node_ids:
            with self.subTest(
                node_id=value
            ):
                with self.assertRaises(
                    ClusterConfigError
                ):
                    validate_cluster_config(
                        replace(
                            self.node_config(),
                            node_id=value,
                        )
                    )

        for value in invalid_enrollment_ids:
            with self.subTest(
                enrollment_id=value
            ):
                with self.assertRaises(
                    ClusterConfigError
                ):
                    validate_cluster_config(
                        replace(
                            self.node_config(),
                            enrollment_id=value,
                        )
                    )

    def test_node_rejects_invalid_generation(self) -> None:
        for value in (
            0,
            -1,
            MAX_GENERATION + 1,
            True,
        ):
            with self.subTest(
                generation=value
            ):
                with self.assertRaises(
                    ClusterConfigError
                ):
                    validate_cluster_config(
                        replace(
                            self.node_config(),
                            enrollment_generation=value,
                        )
                    )

    def test_node_rejects_invalid_authorities(self) -> None:
        invalid_urls = (
            "http://director.example.com",
            "https://user@director.example.com",
            "https://director.example.com/api",
            "https://director.example.com/%2f",
            " https://director.example.com",
            "https://director.example.com?x=1",
        )

        for value in invalid_urls:
            with self.subTest(
                director_url=value
            ):
                with self.assertRaises(
                    ClusterConfigError
                ):
                    validate_cluster_config(
                        replace(
                            self.node_config(),
                            director_url=value,
                        )
                    )

    def test_node_rejects_unsafe_paths(self) -> None:
        invalid_paths = (
            "relative/credential",
            "/var/lib/pqnas/../credential",
            "/var/lib/pqnas//credential",
            "/var/lib/pqnas/cluster/",
            "/",
        )

        for value in invalid_paths:
            with self.subTest(
                credential_path=value
            ):
                with self.assertRaises(
                    ClusterConfigError
                ):
                    validate_cluster_config(
                        replace(
                            self.node_config(),
                            cluster_credential_file=value,
                        )
                    )

    def test_node_rejects_shared_secret_paths(self) -> None:
        config = self.node_config()

        with self.assertRaises(
            ClusterConfigError
        ):
            validate_cluster_config(
                replace(
                    config,
                    bootstrap_token_file=(
                        config.cluster_credential_file
                    ),
                )
            )

    def test_duplicate_and_whitespace_keys_fail(
        self,
    ) -> None:
        cases = (
            (
                "PQNAS_SERVER_ROLE=standalone\n"
                "PQNAS_SERVER_ROLE=director\n"
            ),
            "PQNAS_SERVER_ROLE=director \n",
            " PQNAS_SERVER_ROLE=director\n",
        )

        for text in cases:
            with self.subTest(
                text=text
            ):
                with self.assertRaises(
                    ClusterConfigError
                ):
                    parse_cluster_config_from_env_text(
                        text
                    )

    def test_safe_env_file_loads_cluster_config(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory) / "private"
            parent.mkdir(mode=0o700)
            parent.chmod(0o700)
            path = parent / "pqnas.env"

            atomic_write_private_file(
                str(path),
                (
                    "PQNAS_CONFIG=/etc/pqnas\n"
                    "PQNAS_SERVER_ROLE=director\n"
                ),
                owner_uid=self.owner_uid,
                owner_gid=self.owner_gid,
                replace_existing=False,
            )

            self.assertEqual(
                load_existing_cluster_config(
                    str(path),
                    owner_uid=self.owner_uid,
                    owner_gid=self.owner_gid,
                ),
                ClusterInstallConfig(
                    role="director"
                ),
            )


if __name__ == "__main__":
    unittest.main()
