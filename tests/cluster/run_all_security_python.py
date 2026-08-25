#!/usr/bin/env python3
"""Run the repository's Python security and installer regression suite."""

from __future__ import annotations

from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]

TEST_ROOTS = (
    ROOT / "tests" / "cluster",
    ROOT / "tests" / "cookies",
    ROOT / "tests" / "installer",
)


def discover_tests() -> list[Path]:
    tests: list[Path] = []

    for root in TEST_ROOTS:
        tests.extend(
            sorted(
                path
                for path in root.glob("test_*.py")
                if path.is_file()
            )
        )

    return tests


def main() -> int:
    tests = discover_tests()

    if not tests:
        print(
            "FAIL: no Python security/installer tests discovered"
        )
        return 1

    failures: list[tuple[Path, int]] = []

    for path in tests:
        relative = path.relative_to(ROOT)

        print()
        print(
            f"===== {relative} =====",
            flush=True,
        )

        result = subprocess.run(
            [
                sys.executable,
                str(path),
            ],
            cwd=ROOT,
        )

        if result.returncode != 0:
            failures.append(
                (
                    relative,
                    result.returncode,
                )
            )

    print()
    print(
        "===== SECURITY PYTHON SUITE SUMMARY ====="
    )
    print(
        f"tests run: {len(tests)}"
    )
    print(
        f"failures: {len(failures)}"
    )

    for path, code in failures:
        print(
            f"FAIL {code}: {path}"
        )

    if failures:
        return 1

    print(
        "OK: all Python security/installer tests passed"
    )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
