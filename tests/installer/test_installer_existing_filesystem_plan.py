#!/usr/bin/env python3
from __future__ import annotations

import ast
import json
import os
import types
import unittest
from pathlib import Path
from types import SimpleNamespace
from typing import List, Tuple
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
INSTALLER = ROOT / "tools" / "installer" / "pqnas_install.py"


def load_helpers():
    source = INSTALLER.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(INSTALLER))

    wanted = {
        "detect_existing_filesystem_info",
        "plan_for",
    }
    nodes = [
        item
        for item in tree.body
        if isinstance(item, ast.FunctionDef)
        and item.name in wanted
    ]

    found = {item.name for item in nodes}
    if found != wanted:
        raise RuntimeError(
            f"installer helper functions missing: {sorted(wanted - found)}"
        )

    module = types.ModuleType("pqnas_installer_existing_fs_test")
    module.__dict__.update(
        {
            "InstallState": object,
            "List": List,
            "Tuple": Tuple,
            "json": json,
            "os": os,
            "run_cmd": lambda _argv: "",
            "dev_part_path": lambda disk: f"{disk.path}1",
        }
    )

    isolated = ast.Module(body=nodes, type_ignores=[])
    ast.fix_missing_locations(isolated)
    exec(compile(isolated, str(INSTALLER), "exec"), module.__dict__)
    return module


class InstallerExistingFilesystemPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = load_helpers()
        cls.source = INSTALLER.read_text(encoding="utf-8")

    def test_detection_uses_nearest_existing_parent(self) -> None:
        findmnt_output = json.dumps(
            {
                "filesystems": [
                    {
                        "source": "/dev/sda4",
                        "fstype": "btrfs",
                        "target": "/srv",
                    }
                ]
            }
        )

        def fake_exists(path: str) -> bool:
            return path in ("/", "/srv")

        with mock.patch.object(
            self.module.os.path,
            "exists",
            side_effect=fake_exists,
        ), mock.patch.object(
            self.module,
            "run_cmd",
            return_value=findmnt_output,
        ) as run:
            info = self.module.detect_existing_filesystem_info(
                "/srv/pqnas"
            )

        self.assertEqual(
            info,
            ("/dev/sda4", "btrfs", "/srv"),
        )
        run.assert_called_once_with(
            [
                "findmnt",
                "-J",
                "-T",
                "/srv",
                "-o",
                "SOURCE,FSTYPE,TARGET",
            ]
        )

    def test_detection_failure_is_nonfatal(self) -> None:
        with mock.patch.object(
            self.module,
            "run_cmd",
            side_effect=RuntimeError("findmnt failed"),
        ):
            info = self.module.detect_existing_filesystem_info("/srv")

        self.assertEqual(info, ("", "", ""))

    def test_existing_plan_reports_backing_filesystem(self) -> None:
        state = SimpleNamespace(
            disk=SimpleNamespace(
                path="/dev/sda",
                name="sda",
                is_system=True,
            ),
            mountpoint="/srv/pqnas",
            install_mode="existing",
            backend="btrfs",
        )

        with mock.patch.object(
            self.module,
            "detect_existing_filesystem_info",
            return_value=("/dev/sda4", "btrfs", "/srv"),
        ):
            plan, notes = self.module.plan_for(state)

        self.assertEqual(plan, [])
        self.assertIn(
            "Mode: existing filesystem (NO wipe, NO partitioning).",
            notes,
        )
        self.assertIn("PQ-NAS path: /srv/pqnas", notes)
        self.assertIn("Backing mount: /srv", notes)
        self.assertIn("Filesystem: btrfs", notes)
        self.assertIn("Source: /dev/sda4", notes)
        self.assertIn(
            "WARNING: Existing filesystem capacity is shared with "
            "other data on /srv.",
            notes,
        )

    def test_existing_plan_never_adds_destructive_commands(self) -> None:
        state = SimpleNamespace(
            disk=SimpleNamespace(
                path="/dev/sda",
                name="sda",
                is_system=True,
            ),
            mountpoint="/srv/pqnas",
            install_mode="existing",
            backend="btrfs",
        )

        with mock.patch.object(
            self.module,
            "detect_existing_filesystem_info",
            return_value=("", "", ""),
        ):
            plan, notes = self.module.plan_for(state)

        self.assertEqual(plan, [])
        self.assertIn("Backing mount: unknown", notes)
        self.assertIn("Filesystem: unknown", notes)
        self.assertIn("Source: unknown", notes)

    def test_old_root_capacity_claim_is_removed(self) -> None:
        self.assertNotIn(
            "This uses your current root filesystem capacity.",
            self.source,
        )
        self.assertNotIn(
            "uses your current root disk capacity",
            self.source,
        )


if __name__ == "__main__":
    unittest.main()
