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

def replace_once(rel: str, old: str, new: str) -> None:
    text = read(rel)
    if new in text:
        print(f"unchanged: {rel}")
        return
    if old not in text:
        die(f"anchor not found in {rel}: {old!r}")
    write(rel, text.replace(old, new, 1))

# 1) Runtime config root: prefer explicit PQNAS_CONFIG_ROOT, then existing
# deployment variable PQNAS_CONFIG, then /etc/pqnas.
replace_once(
    "server/src/runtime_paths.cpp",
    """    std::string config_root_dir() {
        const std::string env = getenv_str_local("PQNAS_CONFIG_ROOT");
        if (!env.empty()) return env;

        return "/etc/pqnas";
    }
""",
    """    std::string config_root_dir() {
        const std::string explicit_root = getenv_str_local("PQNAS_CONFIG_ROOT");
        if (!explicit_root.empty()) return explicit_root;

        // Existing deployments use PQNAS_CONFIG for the runtime config
        // directory. Keep PQNAS_CONFIG_ROOT as the clearer new name, but honor
        // PQNAS_CONFIG so OPAQUE files land beside the rest of PQ-NAS config.
        const std::string legacy_config = getenv_str_local("PQNAS_CONFIG");
        if (!legacy_config.empty()) return legacy_config;

        return "/etc/pqnas";
    }
""",
)

# 2) Tests: unset PQNAS_CONFIG too.
replace_once(
    "tests/runtime_paths/test_opaque_runtime_paths.cpp",
    """void unset_opaque_env() {
    ::unsetenv("PQNAS_CONFIG_ROOT");
    ::unsetenv("PQNAS_OPAQUE_CREDENTIALS_PATH");
""",
    """void unset_opaque_env() {
    ::unsetenv("PQNAS_CONFIG_ROOT");
    ::unsetenv("PQNAS_CONFIG");
    ::unsetenv("PQNAS_OPAQUE_CREDENTIALS_PATH");
""",
)

# 3) Tests: add PQNAS_CONFIG fallback and precedence check.
replace_once(
    "tests/runtime_paths/test_opaque_runtime_paths.cpp",
    """    require_true(::setenv("PQNAS_CONFIG_ROOT", root.string().c_str(), 1) == 0,
                 "setenv PQNAS_CONFIG_ROOT should succeed");

    require_true(pqnas::config_root_path() == root,
                 "PQNAS_CONFIG_ROOT should override config root");
    require_true(pqnas::opaque_credentials_path() == root / "opaque_credentials.json",
                 "config root override should affect credentials path");
    require_true(pqnas::opaque_server_setup_path() == root / "opaque_server_setup.bin",
                 "config root override should affect server setup path");

    require_true(::setenv("PQNAS_OPAQUE_CREDENTIALS_PATH", "/tmp/custom_opaque_credentials.json", 1) == 0,
""",
    """    const fs::path legacy_root = root / "legacy_pqnas_config";
    require_true(::setenv("PQNAS_CONFIG", legacy_root.string().c_str(), 1) == 0,
                 "setenv PQNAS_CONFIG should succeed");

    require_true(pqnas::config_root_path() == legacy_root,
                 "PQNAS_CONFIG should override config root when PQNAS_CONFIG_ROOT is unset");
    require_true(pqnas::opaque_credentials_path() == legacy_root / "opaque_credentials.json",
                 "PQNAS_CONFIG fallback should affect credentials path");
    require_true(pqnas::opaque_server_setup_path() == legacy_root / "opaque_server_setup.bin",
                 "PQNAS_CONFIG fallback should affect server setup path");

    require_true(::setenv("PQNAS_CONFIG_ROOT", root.string().c_str(), 1) == 0,
                 "setenv PQNAS_CONFIG_ROOT should succeed");

    require_true(pqnas::config_root_path() == root,
                 "PQNAS_CONFIG_ROOT should override PQNAS_CONFIG");
    require_true(pqnas::opaque_credentials_path() == root / "opaque_credentials.json",
                 "config root override should affect credentials path");
    require_true(pqnas::opaque_server_setup_path() == root / "opaque_server_setup.bin",
                 "config root override should affect server setup path");

    require_true(::setenv("PQNAS_OPAQUE_CREDENTIALS_PATH", "/tmp/custom_opaque_credentials.json", 1) == 0,
""",
)

# 4) Design doc update.
replace_once(
    "docs/technical/opaque_login_design.md",
    """- `GET /api/admin/auth/opaque/status` exposes OPAQUE backend diagnostics to admins only; public OPAQUE login endpoints remain generic and fail-closed.
""",
    """- `GET /api/admin/auth/opaque/status` exposes OPAQUE backend diagnostics to admins only; public OPAQUE login endpoints remain generic and fail-closed.
- OPAQUE config paths use `PQNAS_CONFIG_ROOT` when set, otherwise the existing deployment `PQNAS_CONFIG`, otherwise `/etc/pqnas`.
""",
)

print("done")
