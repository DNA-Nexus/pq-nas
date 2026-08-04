#!/usr/bin/env python3
from __future__ import annotations

import os
import stat
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HELPER = ROOT / "tools" / "runtime" / "pqnas-first-admin"


class FirstAdminEnvironmentCleanupTests(
    unittest.TestCase
):
    @staticmethod
    def write_executable(
        path: Path,
        contents: str,
    ) -> None:
        path.write_text(contents, encoding="utf-8")
        path.chmod(0o755)

    def test_cleanup_backups_are_sanitized(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory(
            prefix="pqnas-first-admin-test-"
        ) as directory:
            root = Path(directory)
            fake_bin = root / "bin"
            fake_bin.mkdir()

            env_file = root / "pqnas.env"
            env_file.write_text(
                "PQNAS_LOGIN_MODE=opaque\n"
                "PQNAS_KEEP_SETTING=yes\n"
                "PQNAS_PASSWORD_BOOTSTRAP_TOKEN="
                "password-secret\n"
                "PQNAS_OPAQUE_BOOTSTRAP_TOKEN="
                "opaque-secret\n",
                encoding="utf-8",
            )
            env_file.chmod(0o600)

            self.write_executable(
                fake_bin / "id",
                """#!/usr/bin/env bash
if [ "${1:-}" = "-u" ]; then
  printf '0\\n'
else
  /usr/bin/id "$@"
fi
""",
            )

            self.write_executable(
                fake_bin / "systemctl",
                """#!/usr/bin/env bash
printf '%s\\n' "$*" \
  >> "${PQNAS_TEST_SYSTEMCTL_LOG}"
""",
            )

            self.write_executable(
                fake_bin / "install",
                """#!/usr/bin/env bash
set -eu

source_path=""
destination_path=""

while [ "$#" -gt 0 ]; do
  case "$1" in
    -m|-o|-g)
      shift 2
      ;;
    --)
      shift
      ;;
    -*)
      shift
      ;;
    *)
      if [ -z "$source_path" ]; then
        source_path="$1"
      else
        destination_path="$1"
      fi
      shift
      ;;
  esac
done

cp "$source_path" "$destination_path"
chmod 0600 "$destination_path"
""",
            )

            systemctl_log = root / "systemctl.log"

            environment = os.environ.copy()
            environment.update(
                {
                    "PATH": (
                        str(fake_bin)
                        + ":/usr/bin:/bin"
                    ),
                    "PQNAS_ENV_FILE": str(env_file),
                    "PQNAS_SERVICE": (
                        "pqnas-test.service"
                    ),
                    "PQNAS_TEST_SYSTEMCTL_LOG": (
                        str(systemctl_log)
                    ),
                }
            )

            completed = subprocess.run(
                [str(HELPER), "cleanup"],
                env=environment,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                timeout=15,
                check=False,
            )

            self.assertEqual(
                completed.returncode,
                0,
                completed.stdout + completed.stderr,
            )

            active = env_file.read_text(
                encoding="utf-8"
            )

            self.assertNotIn(
                "PQNAS_PASSWORD_BOOTSTRAP_TOKEN=",
                active,
            )
            self.assertNotIn(
                "PQNAS_OPAQUE_BOOTSTRAP_TOKEN=",
                active,
            )
            self.assertIn(
                "PQNAS_KEEP_SETTING=yes",
                active,
            )

            backups = sorted(
                root.glob("pqnas.env.bak-*")
            )

            self.assertEqual(len(backups), 2)

            for backup in backups:
                contents = backup.read_text(
                    encoding="utf-8"
                )

                self.assertNotIn(
                    "PQNAS_PASSWORD_BOOTSTRAP_TOKEN=",
                    contents,
                )
                self.assertNotIn(
                    "PQNAS_OPAQUE_BOOTSTRAP_TOKEN=",
                    contents,
                )
                self.assertIn(
                    "PQNAS_KEEP_SETTING=yes",
                    contents,
                )
                self.assertEqual(
                    stat.S_IMODE(
                        backup.stat().st_mode
                    ),
                    0o600,
                )

            self.assertIn(
                "restart pqnas-test.service",
                systemctl_log.read_text(
                    encoding="utf-8"
                ),
            )


if __name__ == "__main__":
    unittest.main()
