#!/usr/bin/env python3
from __future__ import annotations

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "tools" / "installer" / "pqnas_install.py"


class InstallerClusterRoleUiTests(unittest.TestCase):
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
        match = pattern.search(
            self.source
        )
        self.assertIsNotNone(
            match,
            name,
        )
        return match.group(0)

    def test_role_screen_is_between_backend_and_plan(
        self,
    ) -> None:
        backend = self.class_body(
            "BackendSelectScreen"
        )
        role = self.class_body(
            "ClusterRoleScreen"
        )

        self.assertIn(
            "app.push_screen(ClusterRoleScreen())",
            backend,
        )
        self.assertIn(
            "app.push_screen(PlanScreen())",
            role,
        )
        self.assertLess(
            self.source.index(
                "class BackendSelectScreen"
            ),
            self.source.index(
                "class ClusterRoleScreen"
            ),
        )
        self.assertLess(
            self.source.index(
                "class ClusterRoleScreen"
            ),
            self.source.index(
                "class PlanScreen"
            ),
        )

    def test_wizard_uses_seven_steps(self) -> None:
        for step in range(1, 8):
            self.assertIn(
                f"Step {step}/7",
                self.source,
            )

        self.assertNotRegex(
            self.source,
            r"Step [1-7]/6",
        )

    def test_secret_input_is_password_protected(
        self,
    ) -> None:
        role = self.class_body(
            "ClusterRoleScreen"
        )

        self.assertIn(
            "password=True",
            role,
        )
        self.assertIn(
            "validate_node_bootstrap_token(",
            role,
        )
        self.assertNotIn(
            "bootstrap_token_value",
            self.source,
        )

    def test_install_state_keeps_secret_out_of_cluster_config(
        self,
    ) -> None:
        self.assertIn(
            "original_cluster_config: ClusterInstallConfig",
            self.source,
        )
        self.assertIn(
            "node_bootstrap_token: Optional[bytearray]",
            self.source,
        )
        self.assertNotIn(
            "PQNAS_NODE_BOOTSTRAP_TOKEN=",
            self.source,
        )

    def test_secret_is_applied_after_service_stop(
        self,
    ) -> None:
        stop_index = self.source.index(
            'self.logw.write("Stopping pqnas.service (if running) …")'
        )
        layout_index = self.source.index(
            "create_pqnas_layout(mp)"
        )
        secret_index = self.source.index(
            "apply_cluster_bootstrap_secret("
        )
        env_index = self.source.index(
            "write_env_file(\n"
            "                mp,"
        )
        start_index = self.source.index(
            'run_systemctl(["enable", "--now", "pqnas.service"])'
        )

        self.assertLess(
            stop_index,
            layout_index,
        )
        self.assertLess(
            layout_index,
            secret_index,
        )
        self.assertLess(
            secret_index,
            env_index,
        )
        self.assertLess(
            env_index,
            start_index,
        )

    def test_loaded_config_is_preserved_for_cleanup(
        self,
    ) -> None:
        self.assertIn(
            "self.state.original_cluster_config = loaded_cluster_config",
            self.source,
        )
        self.assertIn(
            "self.state.cluster_config = loaded_cluster_config",
            self.source,
        )
        self.assertIn(
            "st.original_cluster_config",
            self.source,
        )

    def test_execute_failures_wipe_pending_secret(
        self,
    ) -> None:
        execute = self.class_body(
            "ExecuteScreen"
        )

        self.assertIn(
            "if not st.disk:\n"
            "            wipe_secret_buffer(\n"
            "                st.node_bootstrap_token\n"
            "            )\n"
            "            st.node_bootstrap_token = None",
            execute,
        )
        self.assertIn(
            "except Exception as e:\n"
            "            # Security: failures before the token-file write",
            execute,
        )
        self.assertIn(
            "wipe_secret_buffer(\n"
            "                st.node_bootstrap_token\n"
            "            )\n"
            "            st.node_bootstrap_token = None\n"
            "            self.logw.write(f\"[FINALIZE ERROR] {e}\")",
            execute,
        )


if __name__ == "__main__":
    unittest.main()
