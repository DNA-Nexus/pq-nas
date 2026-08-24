#!/usr/bin/env python3

"""Read main.cpp together with locally included .inc implementation files.

Security wiring guards use this helper when asserting that a forbidden route,
service or endpoint is absent from the production translation unit.

This is deliberately scoped to quoted .inc includes. Normal headers are not
expanded because RT-011 concerns implementation blocks compiled into main.cpp.
"""

from pathlib import Path
import re


_LOCAL_INC = re.compile(
    r'(?m)^[ \t]*#[ \t]*include[ \t]+'
    r'"([^"\n]+\.inc)"'
    r'[ \t]*(?://.*)?$'
)


def read_translation_unit_with_local_inc_files(
    entry: Path,
) -> str:
    entry = entry.resolve()
    allowed_root = entry.parent.resolve()

    visited = set()
    chunks = []

    def visit(path: Path) -> None:
        resolved = path.resolve()

        try:
            resolved.relative_to(allowed_root)
        except ValueError as exc:
            raise ValueError(
                "local .inc escapes translation-unit source root: "
                f"{resolved}"
            ) from exc

        if resolved in visited:
            return

        if not resolved.is_file():
            raise FileNotFoundError(
                f"included .inc file does not exist: {resolved}"
            )

        visited.add(resolved)

        text = resolved.read_text(
            encoding="utf-8"
        )

        chunks.append(
            "\n"
            f"// BEGIN RT011 SOURCE: {resolved}\n"
            f"{text}\n"
            f"// END RT011 SOURCE: {resolved}\n"
        )

        for include_name in _LOCAL_INC.findall(text):
            visit(
                resolved.parent /
                include_name
            )

    visit(entry)

    return "".join(chunks)


def read_main_translation_unit_text(
    root: Path = Path("."),
) -> str:
    return read_translation_unit_with_local_inc_files(
        root /
        "server/src/main.cpp"
    )
