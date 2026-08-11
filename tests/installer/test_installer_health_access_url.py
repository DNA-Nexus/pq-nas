#!/usr/bin/env python3
from __future__ import annotations

import ast
import types
import unittest
from pathlib import Path
from typing import Optional


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "tools" / "installer" / "pqnas_install.py"


def load_health_helper():
    source = INSTALLER.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(INSTALLER))

    node = next(
        (
            item
            for item in tree.body
            if isinstance(item, ast.FunctionDef)
            and item.name == "installer_health_access_target"
        ),
        None,
    )
    if node is None:
        raise RuntimeError("installer_health_access_target function not found")

    module = types.ModuleType("pqnas_installer_health_test")
    module.__dict__.update({"Optional": Optional})

    isolated = ast.Module(body=[node], type_ignores=[])
    ast.fix_missing_locations(isolated)
    exec(compile(isolated, str(INSTALLER), "exec"), module.__dict__)
    return module


class InstallerHealthAccessUrlTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_health_helper()
        cls.source = INSTALLER.read_text(encoding="utf-8")

    def test_https_public_origin_is_shown_as_https(self) -> None:
        label, url, note = self.module.installer_health_access_target(
            "https://storage.example.test",
            "storage",
        )

        self.assertEqual(label, "Access URL")
        self.assertEqual(url, "https://storage.example.test/")
        self.assertEqual(note, "")

    def test_http_public_origin_remains_http(self) -> None:
        label, url, note = self.module.installer_health_access_target(
            "http://storage.example.test",
            "storage",
        )

        self.assertEqual(label, "Access URL")
        self.assertEqual(url, "http://storage.example.test/")
        self.assertEqual(note, "")

    def test_standalone_without_public_origin_uses_direct_listener(self) -> None:
        label, url, note = self.module.installer_health_access_target(
            None,
            "standalone",
        )

        self.assertEqual(label, "Access URL")
        self.assertEqual(url, "http://<server-ip>:8081/")
        self.assertIn("port 8081", note)

    def test_cluster_role_without_public_origin_stays_loopback_only(self) -> None:
        label, url, note = self.module.installer_health_access_target(
            None,
            "storage",
        )

        self.assertEqual(label, "Local service URL")
        self.assertEqual(url, "http://127.0.0.1:8081/ (loopback only)")
        self.assertIn("secure tunnel", note)

    def test_health_screen_receives_resolved_installer_origin(self) -> None:
        self.assertIn(
            "public_origin=installer_public_origin(st)",
            self.source,
        )


if __name__ == "__main__":
    unittest.main()
