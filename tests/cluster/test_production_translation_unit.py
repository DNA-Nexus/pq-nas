#!/usr/bin/env python3

from pathlib import Path
from tempfile import TemporaryDirectory

from production_translation_unit import (
    read_main_translation_unit_text,
    read_translation_unit_with_local_inc_files,
)


with TemporaryDirectory(
    prefix="pqnas-rt011-tu-"
) as temporary:
    root = Path(temporary)

    (root / "routes" / "nested").mkdir(
        parents=True
    )

    main = root / "main.cpp"
    first = root / "routes" / "first.inc"
    second = (
        root /
        "routes" /
        "nested" /
        "second.inc"
    )

    main.write_text(
        'MAIN_SENTINEL\n'
        '#include "routes/first.inc"\n',
        encoding="utf-8",
    )

    first.write_text(
        'FIRST_INC_SENTINEL\n'
        '#include "nested/second.inc"\n',
        encoding="utf-8",
    )

    second.write_text(
        'SECOND_INC_SENTINEL\n',
        encoding="utf-8",
    )

    expanded = (
        read_translation_unit_with_local_inc_files(
            main
        )
    )

    assert "MAIN_SENTINEL" in expanded
    assert "FIRST_INC_SENTINEL" in expanded
    assert "SECOND_INC_SENTINEL" in expanded


# Repository regression: production main.cpp currently contains local .inc
# implementation blocks. The security view must therefore be larger than the
# literal main.cpp and include at least one known route implementation source.
main_text = Path(
    "server/src/main.cpp"
).read_text(
    encoding="utf-8"
)

production_tu = (
    read_main_translation_unit_text()
)

assert production_tu != main_text
assert len(production_tu) > len(main_text)

assert (
    'srv.Put("/api/v4/files/put",' not in main_text
), (
    "RT-011 repository fixture unexpectedly moved the PUT implementation "
    "back into literal main.cpp"
)

assert (
    'srv.Put("/api/v4/files/put",' in production_tu
), (
    "RT-011 production translation-unit view did not expand "
    "routes_files_put.inc implementation content"
)


# Security regression: absence assertions against the literal main.cpp can be
# bypassed by moving production wiring into a locally included .inc file.
for guard in sorted(
    Path("tests/cluster").glob("*.py")
):
    if guard.resolve() == Path(__file__).resolve():
        continue

    guard_text = guard.read_text(
        encoding="utf-8"
    )

    assert " not in main" not in guard_text, (
        "RT-011 main.cpp-only negative wiring assertion found in "
        f"{guard}"
    )


print(
    "OK: production translation-unit .inc expansion guards passed"
)
