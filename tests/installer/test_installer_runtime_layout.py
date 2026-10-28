#!/usr/bin/env python3
from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "tools" / "installer" / "pqnas_install.py"


class InstallerRuntimeLayoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.source = INSTALLER.read_text(encoding="utf-8")

    def function_body(self, name: str) -> str:
        pattern = re.compile(
            rf"(?ms)^def {re.escape(name)}\(.*?(?=^def |^class |\Z)"
        )
        match = pattern.search(self.source)
        self.assertIsNotNone(match, name)
        return match.group(0)

    def test_mutable_config_uses_storage_root(self) -> None:
        body = self.function_body("ensure_config_files")

        self.assertIn(
            "config_dir = prepare_mutable_config_directory(",
            body,
        )
        self.assertIn(
            "owner_uid, owner_gid = service_owner_ids",
            body,
        )
        self.assertNotIn(
            'd = os.path.join(etc_dir, name)',
            body,
        )
        self.assertIn(
            "Legacy mutable configuration requires manual migration",
            body,
        )
        self.assertNotIn("PQNAS_FORCE_CONFIG", body)

    def test_environment_points_to_storage_config(self) -> None:
        body = self.function_body("write_env_file")

        self.assertIn(
            'config_root = os.path.join(root, "config")',
            body,
        )
        self.assertIn(
            'f"PQNAS_CONFIG={config_root}"',
            body,
        )
        self.assertIn(
            'f"PQNAS_CONFIG_ROOT={config_root}"',
            body,
        )
        self.assertNotIn(
            '"PQNAS_CONFIG=/etc/pqnas"',
            body,
        )


    def test_runtime_layout_avoids_recursive_chown(
        self,
    ) -> None:
        body = self.function_body("create_pqnas_layout")

        self.assertIn(
            "prepare_storage_runtime_layout(",
            body,
        )
        self.assertIn(
            "prepare_service_state_directory(",
            body,
        )
        self.assertIn(
            "if not os.path.lexists(root):",
            body,
        )
        self.assertIn(
            "prepare_service_state_directory(\n"
            "            root,",
            body,
        )

        root_prepare_index = body.index(
            "prepare_service_state_directory(\n"
            "            root,"
        )
        layout_index = body.index(
            "prepare_storage_runtime_layout("
        )
        self.assertLess(
            root_prepare_index,
            layout_index,
        )
        self.assertNotIn(
            "ensure_pqnas_runtime_ownership",
            self.source,
        )
        self.assertNotIn(
            '["chown", "-R", "pqnas:pqnas", root]',
            self.source,
        )

    def test_systemd_does_not_make_etc_writable(self) -> None:
        body = self.function_body("write_systemd_unit")
        list_match = re.search(
            r"writable_paths = .*?\]\)\)",
            body,
            re.DOTALL,
        )

        self.assertIsNotNone(list_match)
        self.assertNotIn(
            '"/etc/pqnas"',
            list_match.group(0),
        )



    def test_service_stops_before_runtime_layout_changes(
        self,
    ) -> None:
        user_index = self.source.index(
            'self.logw.write("Ensuring pqnas service user …")'
        )
        stop_index = self.source.index(
            'self.logw.write("Stopping pqnas.service (if running) …")'
        )
        layout_index = self.source.index(
            "create_pqnas_layout(mp)"
        )
        config_index = self.source.index(
            "ensure_config_files(mp, asset_root)"
        )

        self.assertLess(user_index, stop_index)
        self.assertLess(stop_index, layout_index)
        self.assertLess(layout_index, config_index)
        self.assertNotIn(
            'subprocess.run(["mkdir", "-p", mp], check=False)',
            self.source,
        )

    def test_opaque_config_uses_managed_writes(self) -> None:
        body = self.function_body("ensure_opaque_runtime_config")

        self.assertIn(
            "atomic_write_managed_text(",
            body,
        )
        self.assertIn(
            "ensure_managed_binary_created(",
            body,
        )
        self.assertNotIn(
            'with open(tmp, "w"',
            body,
        )
        self.assertNotIn(
            '["chown", "pqnas:pqnas"',
            body,
        )

    def test_mutable_install_calls_use_storage_config(
        self,
    ) -> None:
        self.assertIn(
            'os.path.join(mp, "config", "admin_settings.json")',
            self.source,
        )
        self.assertIn(
            'ensure_opaque_runtime_config(\n'
            '                    os.path.join(mp, "config"),',
            self.source,
        )
        self.assertIn(
            "create_pqnas_layout(mp)",
            self.source,
        )
        self.assertNotIn(
            "ensure_pqnas_runtime_ownership",
            self.source,
        )
        self.assertNotIn(
            "ensure_pqnas_ownership",
            self.source,
        )


    def test_cluster_secret_is_not_written_to_environment(
        self,
    ) -> None:
        body = self.function_body(
            "write_env_file"
        )

        self.assertNotIn(
            "PQNAS_NODE_BOOTSTRAP_TOKEN=",
            body,
        )
        self.assertIn(
            "render_cluster_env_lines(",
            body,
        )

    def test_cluster_secret_apply_precedes_environment_write(
        self,
    ) -> None:
        stop_index = self.source.index(
            'self.logw.write("Stopping pqnas.service (if running) …")'
        )
        apply_index = self.source.index(
            "apply_cluster_bootstrap_secret("
        )
        env_index = self.source.index(
            "write_env_file(\n"
            "                mp,"
        )

        self.assertLess(
            stop_index,
            apply_index,
        )
        self.assertLess(
            apply_index,
            env_index,
        )



if __name__ == "__main__":
    unittest.main()
