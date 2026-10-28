#!/usr/bin/env python3

from pathlib import Path
import argparse
import os
import subprocess
import sys
import tempfile


SPECIAL_HARNESSES = {
    "test_opaque_helper_client":
        "run_test_opaque_helper_client",

    "test_opaque_helper_client_rust_roundtrip":
        "run_test_opaque_helper_client_rust_roundtrip",
}


def tail(text: str, lines: int = 40) -> str:
    parts = text.splitlines()
    return "\n".join(parts[-lines:])


def run_command(
    command,
    cwd: Path,
    timeout: int,
):
    return subprocess.run(
        command,
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        timeout=timeout,
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Run all built C++ regression binaries, using "
            "their required harnesses where direct invocation "
            "is not valid."
        )
    )

    parser.add_argument(
        "--build-dir",
        default="build",
        help="CMake build directory (default: build)",
    )

    parser.add_argument(
        "--timeout",
        type=int,
        default=30,
        help="timeout for a normal test binary in seconds",
    )

    parser.add_argument(
        "--harness-timeout",
        type=int,
        default=300,
        help="timeout for a parameterized CMake harness",
    )

    args = parser.parse_args()

    root = Path(__file__).resolve().parents[2]
    build_dir = (root / args.build_dir).resolve()
    bin_dir = build_dir / "bin"

    if not bin_dir.is_dir():
        print(
            f"FAIL: build bin directory not found: {bin_dir}",
            file=sys.stderr,
        )
        return 1

    tests = sorted(
        path
        for path in bin_dir.glob("test_*")
        if path.is_file() and
        os.access(path, os.X_OK)
    )

    if not tests:
        print(
            f"FAIL: no executable tests found in {bin_dir}",
            file=sys.stderr,
        )
        return 1

    log_dir = Path(
        tempfile.mkdtemp(
            prefix="pqnas-cpp-regressions-"
        )
    )

    passed = []
    failed = []
    timed_out = []

    print(
        f"discovered C++ regression executables: "
        f"{len(tests)}"
    )

    for index, test in enumerate(tests, start=1):
        name = test.name

        print(
            f"[{index}/{len(tests)}] {name}",
            flush=True,
        )

        if name in SPECIAL_HARNESSES:
            command = [
                "cmake",
                "--build",
                str(build_dir),
                "--target",
                SPECIAL_HARNESSES[name],
            ]
            timeout = args.harness_timeout
        else:
            command = [str(test)]
            timeout = args.timeout

        try:
            result = run_command(
                command,
                root,
                timeout,
            )

            output = result.stdout or ""
            (log_dir / f"{name}.log").write_text(
                output
            )

            if result.returncode == 0:
                passed.append(name)
            else:
                failed.append(
                    (
                        name,
                        result.returncode,
                        output,
                    )
                )

        except subprocess.TimeoutExpired as exc:
            output = exc.stdout or ""

            if isinstance(output, bytes):
                output = output.decode(
                    errors="replace"
                )

            (log_dir / f"{name}.log").write_text(
                output
            )

            timed_out.append(
                (
                    name,
                    output,
                )
            )

    print()
    print("===== C++ REGRESSION SUMMARY =====")
    print(f"PASS:    {len(passed)}")
    print(f"FAIL:    {len(failed)}")
    print(f"TIMEOUT: {len(timed_out)}")

    for name, rc, output in failed:
        print()
        print(
            f"===== FAIL: {name} rc={rc} ====="
        )
        print(tail(output))

    for name, output in timed_out:
        print()
        print(
            f"===== TIMEOUT: {name} ====="
        )
        print(tail(output))

    print()
    print(f"logs: {log_dir}")

    if failed or timed_out:
        return 1

    print(
        "OK: all C++ regression tests passed"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
