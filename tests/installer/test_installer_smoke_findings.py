#!/usr/bin/env python3
from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = (
    ROOT
    / "tools"
    / "installer"
    / "pqnas_install.py"
)


class InstallerSmokeFindingTests(
    unittest.TestCase
):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = INSTALLER.read_text(
            encoding="utf-8"
        )

    def class_body(self, name: str) -> str:
        pattern = re.compile(
            rf"(?ms)^class {re.escape(name)}\(.*?"
            rf"(?=^class |\Z)"
        )
        match = pattern.search(self.source)
        self.assertIsNotNone(match, name)
        return match.group(0)

    def function_body(self, name: str) -> str:
        pattern = re.compile(
            rf"(?ms)^def {re.escape(name)}\(.*?"
            rf"(?=^def |^class |\Z)"
        )
        match = pattern.search(self.source)
        self.assertIsNotNone(match, name)
        return match.group(0)

    def test_nginx_health_check_is_optional(
        self,
    ) -> None:
        helper = self.function_body(
            "nginx_health_check_output"
        )
        health = self.class_body(
            "HealthScreen"
        )

        self.assertIn(
            "if not nginx_enabled:",
            helper,
        )
        self.assertIn(
            'shutil.which("nginx")',
            helper,
        )
        self.assertIn(
            "subprocess.SubprocessError",
            helper,
        )
        self.assertIn(
            "disabled=not self.nginx_enabled",
            health,
        )
        self.assertEqual(
            health.count(
                "nginx_health_check_output("
            ),
            2,
        )
        self.assertNotIn(
            'run_cmd_capture(["nginx", "-t"])',
            health,
        )

    def test_health_screen_is_role_aware(
        self,
    ) -> None:
        health = self.class_body(
            "HealthScreen"
        )

        self.assertIn(
            "self.server_role",
            health,
        )
        self.assertIn(
            'role == "standalone"',
            health,
        )
        self.assertIn(
            "http://127.0.0.1:8081/",
            health,
        )
        self.assertIn(
            "loopback only",
            health,
        )
        self.assertIn(
            "server_role=st.cluster_config.role",
            self.source,
        )

    def test_health_screen_does_not_disclose_token(
        self,
    ) -> None:
        health = self.class_body(
            "HealthScreen"
        )

        self.assertNotIn(
            "password_bootstrap_token",
            health,
        )
        self.assertNotIn(
            "X-PQNAS-Bootstrap-Token",
            health,
        )
        self.assertIn(
            "sudo pqnas-first-admin",
            health,
        )

    def test_health_screen_uses_storage_paths(
        self,
    ) -> None:
        health = self.class_body(
            "HealthScreen"
        )

        self.assertIn(
            "opaque_server_setup.bin",
            health,
        )
        self.assertIn(
            "opaque_credentials.json",
            health,
        )
        self.assertIn(
            'f"  {self.mp}/config/opaque_server_setup.bin"',
            health,
        )
        self.assertIn(
            'f"  {self.mp}/config/opaque_credentials.json"',
            health,
        )
        self.assertNotIn(
            "/etc/pqnas/opaque_credentials.json",
            health,
        )
        self.assertNotIn(
            "/etc/pqnas/opaque_server_setup.bin",
            health,
        )

    def test_reverse_proxy_text_preserves_listener_security(
        self,
    ) -> None:
        screen = self.class_body(
            "ReverseProxyScreen"
        )

        self.assertIn(
            "Director and Storage Node roles remain on",
            screen,
        )
        self.assertIn(
            "127.0.0.1:8081",
            screen,
        )
        self.assertNotIn(
            "No reverse proxy (keep :8081)",
            screen,
        )
        self.assertNotIn(
            "unless a local reverse proxy",
            screen,
        )


if __name__ == "__main__":
    unittest.main()
