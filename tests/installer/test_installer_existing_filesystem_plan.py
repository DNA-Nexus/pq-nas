#!/usr/bin/env python3
from __future__ import annotations

import json
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


class InstallerExistingFilesystemPlanTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.module = installer_core
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

    def test_disk_mode_command_plans_are_preserved(self) -> None:
        disk = SimpleNamespace(
            path="/dev/sdb",
            name="sdb",
            is_system=False,
        )

        common_prefix = [
            ["wipefs", "-a", "/dev/sdb"],
            ["sgdisk", "--zap-all", "/dev/sdb"],
            [
                "dd",
                "if=/dev/zero",
                "of=/dev/sdb",
                "bs=1M",
                "count=32",
                "status=none",
            ],
            ["sync"],
            ["sgdisk", "--zap-all", "/dev/sdb"],
            ["partprobe", "/dev/sdb"],
        ]

        cases = {
            "ext4": common_prefix + [
                [
                    "parted",
                    "/dev/sdb",
                    "--script",
                    "mklabel",
                    "gpt",
                    "mkpart",
                    "primary",
                    "ext4",
                    "1MiB",
                    "100%",
                ],
                ["partprobe", "/dev/sdb"],
                [
                    "mkfs.ext4",
                    "-F",
                    "-L",
                    "PQNAS_DATA",
                    "/dev/sdb1",
                ],
                ["mkdir", "-p", "/srv/pqnas"],
                ["mount", "/dev/sdb1", "/srv/pqnas"],
            ],
            "btrfs": common_prefix + [
                [
                    "parted",
                    "/dev/sdb",
                    "--script",
                    "mklabel",
                    "gpt",
                    "mkpart",
                    "primary",
                    "btrfs",
                    "1MiB",
                    "100%",
                ],
                ["partprobe", "/dev/sdb"],
                [
                    "mkfs.btrfs",
                    "-f",
                    "-L",
                    "PQNAS_DATA",
                    "/dev/sdb1",
                ],
                ["mkdir", "-p", "/srv/pqnas"],
                ["mount", "/dev/sdb1", "/srv/pqnas"],
                [
                    "btrfs",
                    "subvolume",
                    "create",
                    "/srv/pqnas/data",
                ],
                [
                    "btrfs",
                    "subvolume",
                    "create",
                    "/srv/pqnas/snaps",
                ],
                ["umount", "/srv/pqnas"],
                [
                    "mount",
                    "-o",
                    "subvol=data",
                    "/dev/sdb1",
                    "/srv/pqnas",
                ],
            ],
        }

        for backend, expected in cases.items():
            with self.subTest(backend=backend):
                state = SimpleNamespace(
                    disk=disk,
                    mountpoint="/srv/pqnas",
                    install_mode="disk",
                    backend=backend,
                )

                plan, notes = self.module.plan_for(state)

                self.assertEqual(plan, expected)
                self.assertIn(
                    "Disk will be destroyed",
                    "\n".join(notes),
                )

    def test_disk_mode_refuses_system_disk_before_plan(self) -> None:
        state = SimpleNamespace(
            disk=SimpleNamespace(
                path="/dev/sda",
                name="sda",
                is_system=True,
            ),
            mountpoint="/srv/pqnas",
            install_mode="disk",
            backend="btrfs",
        )

        with self.assertRaisesRegex(
            RuntimeError,
            "Refusing to operate on system disk",
        ):
            self.module.plan_for(state)

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
