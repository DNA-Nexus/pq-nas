#!/usr/bin/env python3
"""
Regression test for Update Center plan_hash canonical JSON handling.

C++ uses nlohmann::json::dump() compact UTF-8 JSON for plan_hash.
The Python update helper must validate the same hash when plan fields contain
non-ASCII text, emoji, and Unicode paths.
"""

from __future__ import annotations

import importlib.util
import json
import shutil
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
HELPER_PATH = REPO_ROOT / "server" / "src" / "updates" / "pqnas_update_apply.py"


def load_helper():
    spec = importlib.util.spec_from_file_location("pqnas_update_apply", HELPER_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"failed to load helper from {HELPER_PATH}")

    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def write_plan(path: Path, plan: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(
        json.dumps(plan, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def main() -> int:
    helper = load_helper()

    tmp = Path(tempfile.mkdtemp(prefix="pqnas-update-unicode-plan-"))
    try:
        updates_root = tmp / "updates"
        incoming = updates_root / "incoming"
        plans = updates_root / "plans"
        incoming.mkdir(parents=True)
        plans.mkdir(parents=True)

        stored_name = "unicode-package.tar.gz"
        package_path = incoming / stored_name
        package_path.write_bytes(b"fake package bytes for hash test\n")
        package_sha256 = helper.sha256_file(package_path)

        plan = {
            "stored_name": stored_name,
            "original_name": "päivitys-ääkköset-😀.tar.gz",
            "package_sha256": package_sha256,
            "package_server_version": "0.1.1-åäö",
            "current_server_version": "0.1.0",
            "actions": [
                {
                    "type": "static_file",
                    "action": "update",
                    "source": "pqnas/static/ääkkönen-😀.txt",
                    "target": "/opt/pqnas/static/ääkkönen-😀.txt",
                    "message": "Unicode path and text: åäö 中文 😀",
                }
            ],
            "notes": [
                "Finnish: ääkköset",
                "Emoji: 😀",
                {"cjk": "中文", "arabic": "مرحبا"},
            ],
        }

        canonical = helper.canonical_plan_for_hash(plan)
        plan_hash = helper.sha256_text(helper.json_dumps_compact(canonical))
        plan_id = plan_hash[:16] + "_unicode_package"

        plan["plan_hash"] = plan_hash
        plan["plan_id"] = plan_id

        write_plan(plans / f"{plan_id}.json", plan)

        loaded_plan, loaded_package, actual_sha = helper.load_and_validate_plan(
            plan_id,
            updates_root,
        )

        assert loaded_plan["plan_hash"] == plan_hash
        assert loaded_package == package_path
        assert actual_sha == package_sha256

        # Negative control: escaped-ASCII JSON must not be accepted as the
        # canonical hash for Unicode content.
        bad_plan = dict(plan)
        bad_canonical = helper.canonical_plan_for_hash(bad_plan)
        bad_plan["plan_hash"] = helper.sha256_text(
            json.dumps(bad_canonical, separators=(",", ":"), ensure_ascii=True)
        )
        bad_plan_id = bad_plan["plan_hash"][:16] + "_unicode_bad"
        bad_plan["plan_id"] = bad_plan_id
        write_plan(plans / f"{bad_plan_id}.json", bad_plan)

        try:
            helper.load_and_validate_plan(bad_plan_id, updates_root)
            raise AssertionError("bad escaped-ASCII unicode plan hash was accepted")
        except RuntimeError as e:
            if "plan_hash mismatch" not in str(e):
                raise

        print("ok: unicode Update Center plan_hash validation matches compact UTF-8 canonical JSON")
        return 0

    finally:
        shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    raise SystemExit(main())
