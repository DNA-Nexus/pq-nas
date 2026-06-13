#!/usr/bin/env python3
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]

def die(msg: str) -> None:
    print(f"ERROR: {msg}", file=sys.stderr)
    sys.exit(1)

def p(rel: str) -> Path:
    return ROOT / rel

def read(rel: str) -> str:
    path = p(rel)
    if not path.exists():
        die(f"missing file: {rel}")
    return path.read_text(encoding="utf-8")

def write(rel: str, text: str) -> None:
    path = p(rel)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

cmake = read("CMakeLists.txt")

if "pqnas_opaque_helper_rust" not in cmake:
    marker = "# -----------------------------------------------------------------------------\n# Test: test_opaque_helper_client\n# -----------------------------------------------------------------------------"
    if marker not in cmake:
        die("CMake insertion marker not found: test_opaque_helper_client section")

    rust_block = """# -----------------------------------------------------------------------------
# Tool: pqnas_opaque_helper_rust
#
# Experimental Rust helper scaffold only:
# - not part of the default ALL build
# - does not replace the current C++ pqnas_opaque_helper yet
# - no production OPAQUE cryptography yet
# - future protocol operations fail closed
# -----------------------------------------------------------------------------
find_program(PQNAS_CARGO cargo)

if (PQNAS_CARGO)
    set(PQNAS_OPAQUE_RUST_DIR "${CMAKE_SOURCE_DIR}/tools/opaque_helper_rust")
    set(PQNAS_OPAQUE_RUST_BIN "${CMAKE_BINARY_DIR}/bin/pqnas_opaque_helper_rust")

    add_custom_target(pqnas_opaque_helper_rust
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/bin"
        COMMAND ${PQNAS_CARGO} build --manifest-path "${PQNAS_OPAQUE_RUST_DIR}/Cargo.toml"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${PQNAS_OPAQUE_RUST_DIR}/target/debug/pqnas_opaque_helper"
            "${PQNAS_OPAQUE_RUST_BIN}"
        BYPRODUCTS "${PQNAS_OPAQUE_RUST_BIN}"
        WORKING_DIRECTORY "${PQNAS_OPAQUE_RUST_DIR}"
        COMMENT "Building experimental Rust OPAQUE helper scaffold"
        VERBATIM
    )

    add_custom_target(run_pqnas_opaque_helper_rust_self_test
        COMMAND "${PQNAS_OPAQUE_RUST_BIN}" self-test
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        DEPENDS pqnas_opaque_helper_rust
    )
else()
    message(WARNING "cargo not found; pqnas_opaque_helper_rust target will not be available")
endif()


"""
    cmake = cmake.replace(marker, rust_block + marker, 1)
    write("CMakeLists.txt", cmake)
else:
    print("unchanged: CMakeLists.txt")

doc_path = "docs/technical/opaque_login_design.md"
doc = read(doc_path)

needle = "- Selected server-side implementation direction: Rust helper binary using `opaque-ke`.\n"
add = "- Experimental Rust helper scaffold exists under `tools/opaque_helper_rust/`; it currently supports only `--version` and `self-test`, while future OPAQUE operations fail closed.\n"

if add not in doc:
    if needle not in doc:
        die("doc anchor not found for Rust helper scaffold status")
    doc = doc.replace(needle, needle + add, 1)
    write(doc_path, doc)
else:
    print(f"unchanged: {doc_path}")

print("done")
