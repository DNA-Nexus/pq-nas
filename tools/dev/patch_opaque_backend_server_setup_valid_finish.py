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
    p(rel).write_text(text, encoding="utf-8")
    print(f"patched: {rel}")

# Remove accidental server-setup-check test from helper-client test.
# That test uses the C++ scaffold helper; backend-status tests cover the new
# client method through a controlled fake helper script instead.
rel = "tests/opaque_helper_client/test_opaque_helper_client.cpp"
text = read(rel)
start = text.find("    const auto tmp_setup = std::filesystem::temp_directory_path() /")
if start != -1:
    end_marker = "    std::filesystem::remove(tmp_setup);\n\n"
    end = text.find(end_marker, start)
    if end == -1:
        die("could not find end of accidental tmp_setup block")
    end += len(end_marker)
    text = text[:start] + text[end:]
    write(rel, text)
else:
    print(f"unchanged: {rel}")

# Finish backend-status fake helper and assertions.
rel = "tests/opaque_backend_status/test_opaque_backend_status.cpp"
text = read(rel)

if "server-setup-check" not in text:
    lines = text.splitlines(keepends=True)
    out = []
    inserted = 0
    block = [
        '               "if [ \\"$1\\" = \\"server-setup-check\\" ]; then\\n"\n',
        '               "  echo \'{\\"ok\\":true,\\"op\\":\\"server-setup-check\\",\\"bytes_read\\":27}\'\\n"\n',
        '               "  exit 0\\n"\n',
        '               "fi\\n"\n',
    ]

    for line in lines:
        if '"exit 9\\n");' in line and inserted < 2:
            out.extend(block)
            inserted += 1
        out.append(line)

    if inserted != 2:
        die(f"expected to insert server-setup-check into 2 fake helpers, inserted {inserted}")

    text = "".join(out)

old = '''    require_true(present.server_setup_file_exists, "server setup file should exist");
    require_true(present.server_setup_file_readable, "server setup file should be readable");
    require_true(present.helper_exists, "helper should exist");
'''
new = '''    require_true(present.server_setup_file_exists, "server setup file should exist");
    require_true(present.server_setup_file_readable, "server setup file should be readable");
    require_true(present.server_setup_valid, "server setup should pass helper validation");
    require_true(present.helper_exists, "helper should exist");
'''
if new not in text:
    if old not in text:
        die("present server_setup assertion anchor not found")
    text = text.replace(old, new, 1)

old = '''    require_true(contains_text(present_diag, "\\"helper_self_test_ok\\":true"),
                 "internal diagnostic should report helper self-test success");
'''
new = '''    require_true(contains_text(present_diag, "\\"helper_self_test_ok\\":true"),
                 "internal diagnostic should report helper self-test success");
    require_true(contains_text(present_diag, "\\"server_setup_valid\\":true"),
                 "internal diagnostic should report server setup validation success");
'''
if new not in text:
    if old not in text:
        die("present diagnostic anchor not found")
    text = text.replace(old, new, 1)

write(rel, text)

print("done")
