#!/usr/bin/env python3
from __future__ import annotations

import ast
import io
import re
import subprocess
import types
import unittest
from pathlib import Path
from typing import List, Optional
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
INSTALLER_DIR = ROOT / "tools" / "installer"
INSTALLER = INSTALLER_DIR / "pqnas_install.py"


def load_installer_module():
    source = INSTALLER.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(INSTALLER))

    apt_node = next(
        (
            node
            for node in tree.body
            if isinstance(node, ast.FunctionDef)
            and node.name == "apt_install"
        ),
        None,
    )
    if apt_node is None:
        raise RuntimeError("apt_install function not found")

    # Keep this regression test independent of Textual and the rest of the
    # installer runtime. Only apt_install() is executed in this test module.
    module = types.ModuleType("pqnas_install_live_log_test")
    module.__dict__.update(
        {
            "List": List,
            "Optional": Optional,
            "Log": object,
            "subprocess": subprocess,
            "have_cmd": lambda _name: True,
        }
    )

    isolated = ast.Module(
        body=[apt_node],
        type_ignores=[],
    )
    ast.fix_missing_locations(isolated)

    exec(
        compile(
            isolated,
            str(INSTALLER),
            "exec",
        ),
        module.__dict__,
    )
    return module


class FakeLog:
    def __init__(self) -> None:
        self.lines: list[str] = []

    def write(self, message: str) -> None:
        self.lines.append(str(message))


class FakeProcess:
    def __init__(
        self,
        lines: list[str],
        returncode: int = 0,
    ) -> None:
        self.stdout = io.StringIO(
            "".join(f"{line}\n" for line in lines)
        )
        self._returncode = returncode

    def wait(self) -> int:
        return self._returncode


class InstallerLiveLogTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_installer_module()
        cls.source = INSTALLER.read_text(encoding="utf-8")

    def execute_screen_body(self) -> str:
        match = re.search(
            r"(?ms)^class ExecuteScreen\(Screen\):.*?"
            r"(?=^# -+\n# App)",
            self.source,
        )
        self.assertIsNotNone(match)
        return match.group(0)

    def test_execute_screen_uses_thread_worker(
        self,
    ) -> None:
        execute = self.execute_screen_body()

        self.assertIn(
            "self.run_worker(",
            execute,
        )
        self.assertIn(
            "self._run_install,",
            execute,
        )
        self.assertIn(
            'name="installer-execute"',
            execute,
        )
        self.assertIn(
            "thread=True",
            execute,
        )
        self.assertNotIn(
            "self.set_timer(0.05, self._run_install)",
            execute,
        )
        self.assertIn(
            "call_from_thread",
            execute,
        )

    def test_apt_install_streams_update_and_install_output(
        self,
    ) -> None:
        log = FakeLog()

        processes = [
            FakeProcess(
                [
                    "Hit:1 repository InRelease",
                    "Reading package lists...",
                ]
            ),
            FakeProcess(
                [
                    "Reading package lists...",
                    "Setting up libexample (1.0)...",
                ]
            ),
        ]

        with (
            mock.patch.object(
                self.module,
                "have_cmd",
                return_value=True,
            ),
            mock.patch.object(
                self.module.subprocess,
                "Popen",
                side_effect=processes,
            ) as popen,
        ):
            self.module.apt_install(
                ["libexample"],
                log=log,
            )

        self.assertEqual(
            log.lines,
            [
                "[*] apt-get update …",
                "Hit:1 repository InRelease",
                "Reading package lists...",
                "[*] apt-get install: libexample …",
                "Reading package lists...",
                "Setting up libexample (1.0)...",
            ],
        )

        self.assertEqual(
            popen.call_args_list[0].args[0],
            ["apt-get", "update", "-y"],
        )
        self.assertEqual(
            popen.call_args_list[1].args[0],
            ["apt-get", "install", "-y", "libexample"],
        )

        for call in popen.call_args_list:
            self.assertIs(
                call.kwargs["stdout"],
                self.module.subprocess.PIPE,
            )
            self.assertIs(
                call.kwargs["stderr"],
                self.module.subprocess.STDOUT,
            )
            self.assertTrue(
                call.kwargs["text"],
            )
            self.assertEqual(
                call.kwargs["bufsize"],
                1,
            )

    def test_apt_install_failure_preserves_streamed_output(
        self,
    ) -> None:
        log = FakeLog()
        failed = FakeProcess(
            [
                "Err:1 repository unavailable",
                "E: Failed to fetch package metadata",
            ],
            returncode=100,
        )

        with (
            mock.patch.object(
                self.module,
                "have_cmd",
                return_value=True,
            ),
            mock.patch.object(
                self.module.subprocess,
                "Popen",
                return_value=failed,
            ) as popen,
        ):
            with self.assertRaises(RuntimeError) as ctx:
                self.module.apt_install(
                    ["libexample"],
                    log=log,
                )

        error = str(ctx.exception)

        self.assertIn(
            "apt-get update failed with exit code 100",
            error,
        )
        self.assertIn(
            "Err:1 repository unavailable",
            error,
        )
        self.assertIn(
            "E: Failed to fetch package metadata",
            error,
        )

        self.assertEqual(
            popen.call_count,
            1,
        )
        self.assertEqual(
            log.lines,
            [
                "[*] apt-get update …",
                "Err:1 repository unavailable",
                "E: Failed to fetch package metadata",
            ],
        )


if __name__ == "__main__":
    unittest.main()
