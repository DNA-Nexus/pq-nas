#!/usr/bin/env python3
from __future__ import annotations

import io
import re
import subprocess
import sys
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
INSTALLER_DIR = ROOT / "tools" / "installer"
INSTALLER = INSTALLER_DIR / "pqnas_install.py"

if str(INSTALLER_DIR) not in sys.path:
    sys.path.insert(0, str(INSTALLER_DIR))

import pqnas_installer_core as installer_core


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
        cls.module = installer_core
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

    def test_apt_install_failure_error_capture_is_bounded(
        self,
    ) -> None:
        log = FakeLog()

        noisy_lines = [
            f"{index:03d}-" + ("x" * 1024)
            for index in range(80)
        ]
        noisy_lines.append("TAIL-MARKER")

        failed = FakeProcess(
            noisy_lines,
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
            ),
        ):
            with self.assertRaises(RuntimeError) as ctx:
                self.module.apt_install(
                    ["libexample"],
                    log=log,
                )

        error = str(ctx.exception)

        self.assertIn(
            "[output truncated; showing last 64 KiB]",
            error,
        )
        self.assertIn(
            "TAIL-MARKER",
            error,
        )
        self.assertLessEqual(
            len(error.encode("utf-8")),
            (64 * 1024) + 512,
        )


if __name__ == "__main__":
    unittest.main()
