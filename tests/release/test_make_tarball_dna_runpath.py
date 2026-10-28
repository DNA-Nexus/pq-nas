#!/usr/bin/env python3
"""Regression tests for release DNA Connect CLI RUNPATH sanitization."""

from __future__ import annotations

import hashlib
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
RELEASE_SCRIPT = REPO_ROOT / "tools" / "release" / "make_tarball.sh"
SOURCE_CLI = (
    REPO_ROOT
    / "third_party"
    / "dna-runtime"
    / "linux-x64"
    / "dna-connect-cli"
)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()

    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)

    return digest.hexdigest()


def dynamic_tags(path: Path) -> str:
    completed = subprocess.run(
        ["readelf", "-d", str(path)],
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        timeout=15,
    )
    return completed.stdout


class ReleaseDnaCliRunpathTests(unittest.TestCase):
    def test_release_script_sanitizes_only_staged_cli(self) -> None:
        script = RELEASE_SCRIPT.read_text(encoding="utf-8")

        copy_marker = (
            'install -m 0755 "$DNA_ALERT_CLI_SRC" '
            '"$STAGED_DNA_ALERT_CLI"'
        )
        sanitize_marker = (
            'patchelf --remove-rpath "$STAGED_DNA_ALERT_CLI"'
        )
        verify_marker = (
            'DNA_ALERT_DYNAMIC_TAGS="$(readelf -d '
            '"$STAGED_DNA_ALERT_CLI")"'
        )

        self.assertIn(
            'STAGED_DNA_ALERT_CLI="$STAGE/runtime/dna/dna-connect-cli"',
            script,
        )
        self.assertIn(copy_marker, script)
        self.assertIn(sanitize_marker, script)
        self.assertIn(verify_marker, script)
        self.assertIn(r"\((RPATH|RUNPATH)\)", script)
        self.assertIn("Shared library: [libdna.so]", script)

        self.assertLess(script.index(copy_marker), script.index(sanitize_marker))
        self.assertLess(
            script.index(sanitize_marker),
            script.index(verify_marker),
        )

        # Security: never patch the repository's third-party source binary.
        self.assertNotIn(
            'patchelf --remove-rpath "$DNA_ALERT_CLI_SRC"',
            script,
        )

    @unittest.skipUnless(
        shutil.which("patchelf") and shutil.which("readelf"),
        "patchelf and readelf are required",
    )
    def test_staged_copy_loses_runpath_and_keeps_libdna_dependency(self) -> None:
        self.assertTrue(SOURCE_CLI.is_file())

        source_hash_before = sha256_file(SOURCE_CLI)

        with tempfile.TemporaryDirectory(
            prefix="pqnas-dna-runpath-test-"
        ) as temp_dir:
            staged_cli = Path(temp_dir) / "dna-connect-cli"
            shutil.copy2(SOURCE_CLI, staged_cli)

            subprocess.run(
                ["patchelf", "--remove-rpath", str(staged_cli)],
                check=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                timeout=15,
            )

            tags = dynamic_tags(staged_cli)

            self.assertNotIn("(RPATH)", tags)
            self.assertNotIn("(RUNPATH)", tags)
            self.assertIn("Shared library: [libdna.so]", tags)

        # Security: release sanitization must not mutate vendor input assets.
        self.assertEqual(source_hash_before, sha256_file(SOURCE_CLI))


if __name__ == "__main__":
    unittest.main(verbosity=2)
