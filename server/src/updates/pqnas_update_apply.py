#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import sys
from pathlib import Path

UPDATES_ROOT_DEFAULT = Path("/var/lib/pqnas/updates")
MAX_PLAN_ID_LEN = 240

PLAN_ID_RE = re.compile(r"^[A-Za-z0-9._-]+$")


def fail(code: str, message: str, http_like_status: int = 400, **extra):
    out = {
        "ok": False,
        "error": code,
        "message": message,
        "status": http_like_status,
    }
    out.update(extra)
    print(json.dumps(out, indent=2, sort_keys=True))
    return 1


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def sha256_text(s: str) -> str:
    return hashlib.sha256(s.encode("utf-8")).hexdigest()


def canonical_plan_for_hash(plan: dict) -> dict:
    canonical = dict(plan)
    canonical.pop("plan_hash", None)
    canonical.pop("plan_id", None)
    canonical.pop("install_contract", None)
    canonical.pop("plan_saved", None)
    canonical.pop("plan_path", None)
    return canonical


def json_dumps_compact(obj) -> str:
    # nlohmann::json::dump() default is compact JSON.
    # Python separators below match compact JSON without spaces.
    return json.dumps(obj, separators=(",", ":"), ensure_ascii=False)


def validate_plan_id(plan_id: str) -> str:
    if not plan_id:
        raise ValueError("missing plan_id")
    if len(plan_id) > MAX_PLAN_ID_LEN:
        raise ValueError("plan_id too long")
    if "/" in plan_id or "\\" in plan_id or ".." in plan_id:
        raise ValueError("bad plan_id path")
    if not PLAN_ID_RE.match(plan_id):
        raise ValueError("bad plan_id characters")
    return plan_id


def is_update_action(action: dict) -> bool:
    a = str(action.get("action", "")).lower()
    return (
        a == "update"
        or a == "update_existing_app"
        or a == "update_existing_app_package"
        or "update" in a
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="PQ-NAS update apply helper")
    parser.add_argument("--plan-id", required=True)
    parser.add_argument(
        "--updates-root",
        default=os.environ.get("PQNAS_UPDATES_ROOT", str(UPDATES_ROOT_DEFAULT)),
    )
    parser.add_argument(
        "--validation-only",
        action="store_true",
        help="Validate the plan and package but do not install anything.",
    )
    args = parser.parse_args()

    try:
        plan_id = validate_plan_id(args.plan_id)
    except ValueError as e:
        return fail("bad_plan_id", str(e))

    updates_root = Path(args.updates_root)
    plans_dir = updates_root / "plans"
    incoming_dir = updates_root / "incoming"

    plan_path = plans_dir / f"{plan_id}.json"
    if not plan_path.is_file():
        return fail("plan_not_found", f"Plan not found: {plan_path}", 404)

    try:
        plan = json.loads(plan_path.read_text(encoding="utf-8"))
    except Exception as e:
        return fail("bad_plan_json", f"Could not parse plan JSON: {e}")

    stored_plan_id = str(plan.get("plan_id", ""))
    if stored_plan_id != plan_id:
        return fail(
            "plan_id_mismatch",
            "Requested plan_id does not match stored plan_id.",
            requested_plan_id=plan_id,
            stored_plan_id=stored_plan_id,
        )

    stored_plan_hash = str(plan.get("plan_hash", ""))
    if not stored_plan_hash:
        return fail("missing_plan_hash", "Plan is missing plan_hash.")

    canonical = canonical_plan_for_hash(plan)
    computed_plan_hash = sha256_text(json_dumps_compact(canonical))

    if computed_plan_hash != stored_plan_hash:
        return fail(
            "plan_hash_mismatch",
            "Plan hash mismatch.",
            stored_plan_hash=stored_plan_hash,
            computed_plan_hash=computed_plan_hash,
        )

    stored_name = str(plan.get("stored_name", ""))
    if not stored_name or "/" in stored_name or "\\" in stored_name or ".." in stored_name:
        return fail("bad_stored_name", "Plan contains invalid stored_name.")

    package_path = incoming_dir / stored_name
    if not package_path.is_file():
        return fail("package_not_found", f"Package not found: {package_path}", 404)

    expected_sha = str(plan.get("package_sha256", ""))
    if not expected_sha:
        return fail("missing_package_sha256", "Plan is missing package_sha256.")

    actual_sha = sha256_file(package_path)
    if actual_sha != expected_sha:
        return fail(
            "package_sha256_mismatch",
            "Package SHA256 mismatch.",
            expected_package_sha256=expected_sha,
            actual_package_sha256=actual_sha,
        )

    actions = plan.get("actions", [])
    if not isinstance(actions, list):
        return fail("bad_actions", "Plan actions is not an array.")

    applicable_actions = [a for a in actions if isinstance(a, dict) and is_update_action(a)]

    reject_actions = [
        a for a in actions
        if isinstance(a, dict) and str(a.get("action", "")).lower() == "reject"
    ]

    if reject_actions:
        return fail(
            "reject_action_present",
            "Plan contains reject actions; refusing install.",
            reject_action_count=len(reject_actions),
        )

    if not applicable_actions:
        return fail(
            "no_applicable_actions",
            "Plan has no installable update actions.",
            plan_id=plan_id,
            stored_name=stored_name,
            package_sha256=actual_sha,
        )

    # Phase 4A intentionally stops here.
    out = {
        "ok": True,
        "validated": True,
        "validation_only": True,
        "install_performed": False,
        "message": "Update helper validated plan and package. Nothing was installed in Phase 4A.",
        "plan_id": plan_id,
        "plan_hash": stored_plan_hash,
        "package_sha256": actual_sha,
        "stored_name": stored_name,
        "package_server_version": plan.get("package_server_version", ""),
        "current_server_version": plan.get("current_server_version", ""),
        "applicable_action_count": len(applicable_actions),
    }

    print(json.dumps(out, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
