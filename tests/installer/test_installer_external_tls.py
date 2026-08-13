#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
INSTALLER_DIR = ROOT / "tools" / "installer"
INSTALLER = INSTALLER_DIR / "pqnas_install.py"

if str(INSTALLER_DIR) not in sys.path:
    sys.path.insert(0, str(INSTALLER_DIR))

import pqnas_installer_core as installer_core


class InstallerExternalTlsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = installer_core
        cls.source = INSTALLER.read_text(encoding="utf-8")

    def state(
        self,
        *,
        nginx_enabled: bool,
        https_enabled: bool = False,
        external_tls_termination: bool = False,
        hostname: str = "storage.example.test",
    ):
        return SimpleNamespace(
            nginx_enabled=nginx_enabled,
            https_enabled=https_enabled,
            external_tls_termination=external_tls_termination,
            nginx_hostname=hostname,
        )

    def test_external_tls_keeps_public_origin_https_without_local_cert(self) -> None:
        st = self.state(
            nginx_enabled=True,
            external_tls_termination=True,
        )

        with mock.patch.object(
            self.module,
            "have_letsencrypt_cert",
            return_value=False,
        ):
            origin = self.module.installer_public_origin(st)

        self.assertEqual(origin, "https://storage.example.test")

    def test_no_local_nginx_preserves_external_https_origin(self) -> None:
        st = self.state(
            nginx_enabled=False,
            https_enabled=False,
            external_tls_termination=False,
        )

        self.assertEqual(
            self.module.installer_public_origin(st),
            "https://storage.example.test",
        )

    def test_http_only_ignores_stale_local_certificate(self) -> None:
        st = self.state(
            nginx_enabled=True,
            https_enabled=False,
            external_tls_termination=False,
        )

        with mock.patch.object(
            self.module,
            "have_letsencrypt_cert",
            return_value=True,
        ):
            origin = self.module.installer_public_origin(st)

        self.assertEqual(origin, "http://storage.example.test")

    def test_local_https_requires_local_certificate_before_initial_https_origin(self) -> None:
        st = self.state(
            nginx_enabled=True,
            https_enabled=True,
        )

        with mock.patch.object(
            self.module,
            "have_letsencrypt_cert",
            return_value=False,
        ):
            self.assertEqual(
                self.module.installer_public_origin(st),
                "http://storage.example.test",
            )

        with mock.patch.object(
            self.module,
            "have_letsencrypt_cert",
            return_value=True,
        ):
            self.assertEqual(
                self.module.installer_public_origin(st),
                "https://storage.example.test",
            )

    def test_external_tls_pin_timeout_is_nonfatal(self) -> None:
        with mock.patch.object(
            self.module,
            "compute_tls_spki_sha256_pin_from_host",
            side_effect=subprocess.TimeoutExpired(
                cmd=["openssl", "s_client"],
                timeout=15,
            ),
        ):
            pin, source = (
                self.module.detect_external_tls_spki_sha256_pin(
                    "storage.example.test"
                )
            )

        self.assertEqual(pin, "")
        self.assertEqual(source, "")

    def test_external_tls_uses_public_host_pin_path(self) -> None:
        self.assertIn(
            "if st.external_tls_termination or not st.nginx_enabled:",
            self.source,
        )
        self.assertIn(
            "detect_external_tls_spki_sha256_pin(",
            self.source,
        )
        self.assertIn(
            "write_nginx_site_http_only(",
            self.source,
        )
        self.assertIn(
            'id="https_external"',
            self.source,
        )
        self.assertIn(
            "(not enabled) or tls_mode == \"https_external\"",
            self.source,
        )


if __name__ == "__main__":
    unittest.main()
