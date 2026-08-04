#!/usr/bin/env python3
from __future__ import annotations

import stat
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INSTALLER_DIR = ROOT / "tools" / "installer"
INSTALLER = INSTALLER_DIR / "pqnas_install.py"

if str(INSTALLER_DIR) not in sys.path:
    sys.path.insert(0, str(INSTALLER_DIR))

from pqnas_static_assets import install_static_assets


def file_mode(path: Path) -> int:
    return stat.S_IMODE(path.lstat().st_mode)


class StaticAssetInstallTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.install_static_assets = staticmethod(
            install_static_assets
        )

    def test_installer_imports_static_asset_module(
        self,
    ) -> None:
        source = INSTALLER.read_text(
            encoding="utf-8"
        )

        self.assertIn(
            "from pqnas_static_assets import "
            "install_static_assets",
            source,
        )
        self.assertNotIn(
            "\ndef install_static_assets(",
            source,
        )

    def test_normalizes_restrictive_package_modes(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset_root = root / "package"
            source = asset_root / "static"
            branding = source / "branding"
            destination = root / "installed-static"

            branding.mkdir(parents=True)

            index = source / "index.html"
            logo = branding / "logo.svg"

            index.write_text(
                "<html>ok</html>\n",
                encoding="utf-8",
            )
            logo.write_text(
                "<svg></svg>\n",
                encoding="utf-8",
            )

            source.chmod(0o700)
            branding.chmod(0o700)
            index.chmod(0o600)
            logo.chmod(0o600)

            destination.mkdir(mode=0o700)

            self.install_static_assets(
                str(asset_root),
                str(destination),
            )

            self.assertEqual(
                file_mode(destination),
                0o755,
            )
            self.assertEqual(
                file_mode(destination / "branding"),
                0o755,
            )
            self.assertEqual(
                file_mode(destination / "index.html"),
                0o644,
            )
            self.assertEqual(
                file_mode(
                    destination
                    / "branding"
                    / "logo.svg"
                ),
                0o644,
            )

            self.assertEqual(
                (destination / "index.html").read_text(
                    encoding="utf-8"
                ),
                "<html>ok</html>\n",
            )

    def test_rejects_packaged_symlink(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset_root = root / "package"
            source = asset_root / "static"
            destination = root / "installed-static"
            outside = root / "outside.txt"

            source.mkdir(parents=True)
            outside.write_text(
                "must not be copied\n",
                encoding="utf-8",
            )

            link = source / "unsafe.html"

            try:
                link.symlink_to(outside)
            except OSError as exc:
                self.skipTest(
                    f"symlink creation unavailable: {exc}"
                )

            with self.assertRaisesRegex(
                RuntimeError,
                "Unsupported static asset entry",
            ):
                self.install_static_assets(
                    str(asset_root),
                    str(destination),
                )

            self.assertFalse(destination.exists())

    def test_rejects_symlink_destination_root(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            asset_root = root / "package"
            source = asset_root / "static"
            real_destination = root / "real-destination"
            destination = root / "destination-link"

            source.mkdir(parents=True)
            (source / "index.html").write_text(
                "ok\n",
                encoding="utf-8",
            )
            real_destination.mkdir()

            try:
                destination.symlink_to(
                    real_destination,
                    target_is_directory=True,
                )
            except OSError as exc:
                self.skipTest(
                    f"symlink creation unavailable: {exc}"
                )

            with self.assertRaisesRegex(
                RuntimeError,
                "destination is not a real directory",
            ):
                self.install_static_assets(
                    str(asset_root),
                    str(destination),
                )

            self.assertEqual(
                list(real_destination.iterdir()),
                [],
            )


if __name__ == "__main__":
    unittest.main()
