#!/usr/bin/env python3
import argparse
import hashlib
import json
import os
import re
import shutil
import stat
import sys
import tarfile
import time
from pathlib import Path

UPDATES_ROOT_DEFAULT = Path("/var/lib/pqnas/updates")
MAX_PLAN_ID_LEN = 240

PLAN_ID_RE = re.compile(r"^[A-Za-z0-9._-]+$")

STATIC_ROOT = Path("/opt/pqnas/static").resolve()
CORE_BINARY = Path("/usr/local/bin/pqnas_server").resolve()
APPS_INSTALLED_ROOT = Path(os.environ.get("PQNAS_APPS_INSTALLED_DIR", "/srv/pqnas/apps/installed")).resolve()


def fail(code: str, message: str, http_like_status: int = 400, **extra) -> int:
    out = {
        "ok": False,
        "error": code,
        "message": message,
        "status": http_like_status,
    }
    out.update(extra)
    print(json.dumps(out, indent=2, sort_keys=True))
    return 1


def ok(**fields) -> int:
    out = {"ok": True}
    out.update(fields)
    print(json.dumps(out, indent=2, sort_keys=True))
    return 0


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


def normalize_archive_path(name: str) -> str:
    s = str(name).replace("\\", "/")
    while s.startswith("./"):
        s = s[2:]
    return s


def is_safe_archive_member(name: str) -> bool:
    s = normalize_archive_path(name)
    if not s or s.startswith("/"):
        return False
    parts = [p for p in s.split("/") if p not in ("", ".")]
    return bool(parts) and ".." not in parts


def safe_extract_tar(package_path: Path, extract_dir: Path) -> list[str]:
    extracted = []

    with tarfile.open(package_path, "r:*") as tf:
        members = tf.getmembers()

        for m in members:
            if not is_safe_archive_member(m.name):
                raise RuntimeError(f"unsafe archive member: {m.name}")

            # Phase 5A only needs regular files and dirs.
            if not (m.isfile() or m.isdir()):
                raise RuntimeError(f"unsupported archive member type: {m.name}")

        tf.extractall(extract_dir)

        for m in members:
            if m.isfile():
                extracted.append(normalize_archive_path(m.name))

    return extracted


def resolve_extracted_source(extract_dir: Path, source: str) -> Path:
    src = normalize_archive_path(source)

    candidates = [
        extract_dir / src,
    ]

    if not src.startswith("pqnas/"):
        candidates.append(extract_dir / "pqnas" / src)

    # Some old plan actions may normalize source without leading pqnas/.
    if src.startswith("pqnas/"):
        candidates.append(extract_dir / src[len("pqnas/"):])

    for c in candidates:
        try:
            resolved = c.resolve()
            root = extract_dir.resolve()
            if not str(resolved).startswith(str(root) + os.sep):
                continue
            if resolved.is_file():
                return resolved
        except Exception:
            continue

    raise FileNotFoundError(f"source not found in extracted package: {source}")


def validate_target_for_apply(action: dict) -> Path:
    typ = str(action.get("type", ""))
    target = Path(str(action.get("target", ""))).resolve()

    if typ == "static_file":
        if not str(target).startswith(str(STATIC_ROOT) + os.sep):
            raise RuntimeError(f"static_file target outside static root: {target}")
        return target

    if typ == "core_binary":
        if target != CORE_BINARY:
            raise RuntimeError(f"core_binary target must be {CORE_BINARY}, got {target}")
        return target

    raise RuntimeError(f"unsupported apply action type: {typ}")




def validate_target_for_dry_run(action: dict) -> Path:
    typ = str(action.get("type", ""))

    if typ in ("static_file", "core_binary"):
        return validate_target_for_apply(action)

    if typ == "bundled_app_package":
        app_id = str(action.get("app_id", "")).strip()
        target = Path(str(action.get("target", ""))).resolve()

        if not app_id:
            raise RuntimeError("bundled_app_package action is missing app_id")

        if not str(target).startswith(str(APPS_INSTALLED_ROOT) + os.sep):
            raise RuntimeError(f"bundled_app_package target outside installed apps root: {target}")

        expected = (APPS_INSTALLED_ROOT / app_id).resolve()
        if target != expected:
            raise RuntimeError(f"bundled_app_package target mismatch: expected {expected}, got {target}")

        return target

    raise RuntimeError(f"unsupported dry-run action type: {typ}")




def strip_known_archive_suffix(name: str) -> str:
    low = name.lower()
    if low.endswith(".tar.gz"):
        return name[:-7]
    if low.endswith(".tgz"):
        return name[:-4]
    if low.endswith(".zip"):
        return name[:-4]
    if low.endswith(".dnxupd"):
        return name[:-7]
    return name


def bundled_app_package_version_from_source(source: str, app_id: str) -> str:
    base = strip_known_archive_suffix(Path(str(source)).name)
    low_base = base.lower()
    low_app = str(app_id).lower()

    for prefix in (low_app + "-", low_app + "_"):
        if low_base.startswith(prefix):
            return base[len(app_id) + 1:]

    return ""

def list_installed_app_versions(app_root: Path) -> list[str]:
    if not app_root.exists() or not app_root.is_dir():
        return []

    versions = []
    for child in app_root.iterdir():
        if child.is_dir():
            versions.append(child.name)

    return sorted(versions)


def backup_target(target: Path, backup_root: Path, manifest_entry: dict) -> None:
    rel = str(target).lstrip("/")
    backup_path = backup_root / "files" / rel
    backup_path.parent.mkdir(parents=True, exist_ok=True)

    manifest_entry["target"] = str(target)
    manifest_entry["backup_path"] = str(backup_path)

    if target.exists():
        shutil.copy2(target, backup_path)
        manifest_entry["existed"] = True
        manifest_entry["backup_sha256"] = sha256_file(backup_path)
    else:
        manifest_entry["existed"] = False



def set_installed_file_metadata(target: Path, typ: str) -> None:
    if typ == "static_file":
        shutil.chown(target, user="pqnas", group="pqnas")
        target.chmod(0o644)
        return

    if typ == "core_binary":
        shutil.chown(target, user="root", group="root")
        target.chmod(0o755)
        return


def atomic_copy_file(src: Path, target: Path, executable: bool = False) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)

    tmp = target.with_name(f".{target.name}.pqnas-update-{os.getpid()}.tmp")
    if tmp.exists():
        tmp.unlink()

    shutil.copy2(src, tmp)

    if executable:
        mode = tmp.stat().st_mode
        tmp.chmod(mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)

    os.replace(tmp, target)


def rollback_applied(manifest: dict) -> list[dict]:
    results = []

    for entry in reversed(manifest.get("targets", [])):
        target = Path(entry["target"])
        backup_path = Path(entry["backup_path"])
        existed = bool(entry.get("existed", False))

        r = {
            "target": str(target),
            "backup_path": str(backup_path),
            "existed": existed,
            "rolled_back": False,
        }

        try:
            if existed:
                if backup_path.is_file():
                    target.parent.mkdir(parents=True, exist_ok=True)
                    os.replace(backup_path, target)
                    r["rolled_back"] = True
                else:
                    r["error"] = "backup_missing"
            else:
                if target.exists():
                    target.unlink()
                r["rolled_back"] = True
        except Exception as e:
            r["error"] = str(e)

        results.append(r)

    return results


def load_and_validate_plan(plan_id: str, updates_root: Path) -> tuple[dict, Path, str]:
    plan_path = updates_root / "plans" / f"{plan_id}.json"
    if not plan_path.is_file():
        raise FileNotFoundError(f"Plan not found: {plan_path}")

    plan = json.loads(plan_path.read_text(encoding="utf-8"))

    stored_plan_id = str(plan.get("plan_id", ""))
    if stored_plan_id != plan_id:
        raise RuntimeError(f"plan_id mismatch: requested={plan_id}, stored={stored_plan_id}")

    stored_plan_hash = str(plan.get("plan_hash", ""))
    if not stored_plan_hash:
        raise RuntimeError("Plan is missing plan_hash.")

    canonical = canonical_plan_for_hash(plan)
    computed_plan_hash = sha256_text(json_dumps_compact(canonical))

    if computed_plan_hash != stored_plan_hash:
        raise RuntimeError(
            f"plan_hash mismatch: stored={stored_plan_hash}, computed={computed_plan_hash}"
        )

    stored_name = str(plan.get("stored_name", ""))
    if not stored_name or "/" in stored_name or "\\" in stored_name or ".." in stored_name:
        raise RuntimeError("Plan contains invalid stored_name.")

    package_path = updates_root / "incoming" / stored_name
    if not package_path.is_file():
        raise FileNotFoundError(f"Package not found: {package_path}")

    expected_sha = str(plan.get("package_sha256", ""))
    if not expected_sha:
        raise RuntimeError("Plan is missing package_sha256.")

    actual_sha = sha256_file(package_path)
    if actual_sha != expected_sha:
        raise RuntimeError(
            f"package SHA256 mismatch: expected={expected_sha}, actual={actual_sha}"
        )

    return plan, package_path, actual_sha


def applicable_actions_from_plan(plan: dict) -> tuple[list[dict], list[dict]]:
    actions = plan.get("actions", [])
    if not isinstance(actions, list):
        raise RuntimeError("Plan actions is not an array.")

    reject_actions = [
        a for a in actions
        if isinstance(a, dict) and str(a.get("action", "")).lower() == "reject"
    ]

    applicable_actions = [
        a for a in actions
        if isinstance(a, dict) and is_update_action(a)
    ]

    return applicable_actions, reject_actions


def run_validation_only(plan: dict,
                        plan_id: str,
                        actual_sha: str,
                        applicable_actions: list[dict],
                        reject_actions: list[dict]) -> int:
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
            plan_hash=plan.get("plan_hash", ""),
            stored_name=plan.get("stored_name", ""),
            package_sha256=actual_sha,
            package_server_version=plan.get("package_server_version", ""),
            current_server_version=plan.get("current_server_version", ""),
            applicable_action_count=0,
            validation_only=True,
            install_performed=False,
        )

    return ok(
        validated=True,
        validation_only=True,
        install_performed=False,
        message="Update helper validated plan and package. Nothing was installed in validation-only mode.",
        plan_id=plan_id,
        plan_hash=plan.get("plan_hash", ""),
        package_sha256=actual_sha,
        stored_name=plan.get("stored_name", ""),
        package_server_version=plan.get("package_server_version", ""),
        current_server_version=plan.get("current_server_version", ""),
        applicable_action_count=len(applicable_actions),
    )



def run_dry_run(plan: dict,
                plan_id: str,
                package_path: Path,
                actual_sha: str,
                updates_root: Path,
                applicable_actions: list[dict],
                reject_actions: list[dict]) -> int:
    if reject_actions:
        return fail(
            "reject_action_present",
            "Plan contains reject actions; refusing dry-run.",
            reject_action_count=len(reject_actions),
            dry_run=True,
            install_performed=False,
        )

    if not applicable_actions:
        return fail(
            "no_applicable_actions",
            "Plan has no installable update actions.",
            plan_id=plan_id,
            plan_hash=plan.get("plan_hash", ""),
            stored_name=plan.get("stored_name", ""),
            package_sha256=actual_sha,
            package_server_version=plan.get("package_server_version", ""),
            current_server_version=plan.get("current_server_version", ""),
            applicable_action_count=0,
            dry_run=True,
            install_performed=False,
        )

    unsupported = []
    for a in applicable_actions:
        typ = str(a.get("type", ""))
        act = str(a.get("action", ""))

        if typ not in ("static_file", "core_binary", "bundled_app_package"):
            unsupported.append({
                "type": typ,
                "action": act,
                "source": a.get("source", ""),
                "target": a.get("target", ""),
                "reason": "Phase 5D dry-run supports static_file, core_binary, and bundled_app_package actions.",
            })

    if unsupported:
        return fail(
            "unsupported_dry_run_actions",
            "Plan contains update actions not supported by Phase 5B dry-run.",
            unsupported_actions=unsupported,
            applicable_action_count=len(applicable_actions),
            dry_run=True,
            install_performed=False,
        )

    ts = time.strftime("%Y%m%d-%H%M%S")
    safe_plan_prefix = re.sub(r"[^A-Za-z0-9._-]", "_", plan_id[:32])
    work_root = updates_root / "work" / f"dryrun_{ts}_{safe_plan_prefix}_{os.getpid()}"
    extract_dir = work_root / "extract"

    planned = []

    try:
        extract_dir.mkdir(parents=True, exist_ok=False)
        safe_extract_tar(package_path, extract_dir)

        for a in applicable_actions:
            target = validate_target_for_dry_run(a)
            source = resolve_extracted_source(extract_dir, str(a.get("source", "")))
            typ = str(a.get("type", ""))

            item = {
                "type": a.get("type", ""),
                "action": a.get("action", ""),
                "app_id": a.get("app_id", ""),
                "source": str(a.get("source", "")),
                "target": str(target),
                "source_path": str(source),
                "source_sha256": sha256_file(source),
                "target_exists": target.exists(),
            }

            if typ == "bundled_app_package":
                app_id = str(a.get("app_id", "")).strip()
                package_version = bundled_app_package_version_from_source(str(a.get("source", "")), app_id)
                if not package_version:
                    raise RuntimeError(f"could not determine bundled app package version for {a.get('source', '')}")

                target_version_dir = (target / package_version).resolve()
                if not str(target_version_dir).startswith(str(target) + os.sep):
                    raise RuntimeError(f"bundled app version target escaped app root: {target_version_dir}")

                item["target_kind"] = "installed_app_version_dir"
                item["package_version"] = package_version
                item["target_app_root"] = str(target)
                item["target_version_dir"] = str(target_version_dir)
                item["installed_versions"] = list_installed_app_versions(target)
                item["target_exists"] = target_version_dir.exists()
                item["would_create_version"] = not target_version_dir.exists()
                item["would_replace_existing_version"] = target_version_dir.exists()
                item["would_replace"] = True
            elif target.exists() and target.is_file():
                item["target_kind"] = "file"
                item["target_sha256"] = sha256_file(target)
                item["would_replace"] = item["source_sha256"] != item["target_sha256"]
            else:
                item["target_kind"] = "missing"
                item["target_sha256"] = ""
                item["would_replace"] = True

            planned.append(item)

        restart_required = any(str(a.get("type", "")) == "core_binary" for a in applicable_actions)

        return ok(
            validated=True,
            dry_run=True,
            validation_only=False,
            install_performed=False,
            restart_required=restart_required,
            message="Dry-run succeeded. No files were modified.",
            plan_id=plan_id,
            plan_hash=plan.get("plan_hash", ""),
            package_sha256=actual_sha,
            stored_name=plan.get("stored_name", ""),
            package_server_version=plan.get("package_server_version", ""),
            current_server_version=plan.get("current_server_version", ""),
            applicable_action_count=len(applicable_actions),
            planned_action_count=len(planned),
            planned_actions=planned,
        )

    except Exception as e:
        return fail(
            "dry_run_failed",
            "Dry-run failed before modifying files. No files were installed.",
            error_detail=str(e),
            dry_run=True,
            install_performed=False,
        )

    finally:
        try:
            if work_root.exists():
                shutil.rmtree(work_root)
        except Exception:
            pass

def run_apply(plan: dict,
              plan_id: str,
              package_path: Path,
              actual_sha: str,
              updates_root: Path,
              applicable_actions: list[dict],
              reject_actions: list[dict]) -> int:
    if reject_actions:
        return fail(
            "reject_action_present",
            "Plan contains reject actions; refusing apply.",
            reject_action_count=len(reject_actions),
        )

    if not applicable_actions:
        return fail(
            "no_applicable_actions",
            "Plan has no installable update actions.",
            plan_id=plan_id,
            plan_hash=plan.get("plan_hash", ""),
            stored_name=plan.get("stored_name", ""),
            package_sha256=actual_sha,
            package_server_version=plan.get("package_server_version", ""),
            current_server_version=plan.get("current_server_version", ""),
            applicable_action_count=0,
            validation_only=False,
            install_performed=False,
        )

    unsupported = []
    for a in applicable_actions:
        typ = str(a.get("type", ""))
        act = str(a.get("action", ""))

        if typ not in ("static_file", "core_binary"):
            unsupported.append({
                "type": typ,
                "action": act,
                "source": a.get("source", ""),
                "target": a.get("target", ""),
                "reason": "Phase 5A apply supports only static_file and core_binary actions.",
            })

    if unsupported:
        return fail(
            "unsupported_apply_actions",
            "Plan contains update actions not supported by Phase 5A apply.",
            unsupported_actions=unsupported,
            applicable_action_count=len(applicable_actions),
        )

    ts = time.strftime("%Y%m%d-%H%M%S")
    safe_plan_prefix = re.sub(r"[^A-Za-z0-9._-]", "_", plan_id[:32])
    work_root = updates_root / "work" / f"{ts}_{safe_plan_prefix}_{os.getpid()}"
    extract_dir = work_root / "extract"
    backup_root = updates_root / "backups" / f"{ts}_{safe_plan_prefix}_{os.getpid()}"

    manifest = {
        "plan_id": plan_id,
        "plan_hash": plan.get("plan_hash", ""),
        "stored_name": plan.get("stored_name", ""),
        "package_sha256": actual_sha,
        "started_at": ts,
        "work_root": str(work_root),
        "backup_root": str(backup_root),
        "targets": [],
        "applied": [],
        "skipped_same": [],
    }

    try:
        extract_dir.mkdir(parents=True, exist_ok=False)
        backup_root.mkdir(parents=True, exist_ok=False)

        safe_extract_tar(package_path, extract_dir)

        # First validate and back up all targets before copying anything.
        prepared = []

        for a in applicable_actions:
            target = validate_target_for_apply(a)
            source = resolve_extracted_source(extract_dir, str(a.get("source", "")))

            source_sha = sha256_file(source)
            entry = {
                "type": a.get("type", ""),
                "action": a.get("action", ""),
                "source": str(a.get("source", "")),
                "target": str(target),
                "source_sha256": source_sha,
            }

            if target.exists() and target.is_file():
                target_sha = sha256_file(target)
                entry["target_sha256"] = target_sha
                if target_sha == source_sha:
                    entry["skipped_same"] = True
                    manifest["skipped_same"].append(entry)
                    continue

            backup_target(target, backup_root, entry)

            prepared.append({
                "action": a,
                "source_path": source,
                "target_path": target,
                "manifest_entry": entry,
            })

            manifest["targets"].append(entry)

        manifest_path = backup_root / "manifest.json"
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        # Apply all prepared copies.
        for item in prepared:
            typ = str(item["action"].get("type", ""))
            src = item["source_path"]
            target = item["target_path"]
            executable = typ == "core_binary"

            atomic_copy_file(src, target, executable=executable)
            set_installed_file_metadata(target, typ)

            applied_entry = {
                "type": typ,
                "target": str(target),
                "installed_sha256": sha256_file(target),
            }
            manifest["applied"].append(applied_entry)

        manifest["completed_at"] = time.strftime("%Y%m%d-%H%M%S")
        manifest["install_performed"] = True
        manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")

        restart_required = any(str(a.get("type", "")) == "core_binary" for a in applicable_actions)

        return ok(
            validated=True,
            validation_only=False,
            install_performed=True,
            restart_required=restart_required,
            message="Update helper applied supported update actions. Restart may be required.",
            plan_id=plan_id,
            plan_hash=plan.get("plan_hash", ""),
            package_sha256=actual_sha,
            stored_name=plan.get("stored_name", ""),
            package_server_version=plan.get("package_server_version", ""),
            current_server_version=plan.get("current_server_version", ""),
            applicable_action_count=len(applicable_actions),
            applied_action_count=len(manifest["applied"]),
            skipped_same_count=len(manifest.get("skipped_same", [])),
            backup_root=str(backup_root),
            manifest_path=str(manifest_path),
        )

    except Exception as e:
        rollback_results = rollback_applied(manifest)

        try:
            backup_root.mkdir(parents=True, exist_ok=True)
            failure_manifest_path = backup_root / "failure_manifest.json"
            manifest["failed_at"] = time.strftime("%Y%m%d-%H%M%S")
            manifest["error"] = str(e)
            manifest["rollback_results"] = rollback_results
            failure_manifest_path.write_text(
                json.dumps(manifest, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
            )
        except Exception:
            failure_manifest_path = None

        return fail(
            "apply_failed_rolled_back",
            "Apply failed and rollback was attempted.",
            error_detail=str(e),
            rollback_results=rollback_results,
            failure_manifest_path=str(failure_manifest_path) if failure_manifest_path else "",
        )

    finally:
        # Keep backup_root. Remove temporary extract/work dir.
        try:
            if work_root.exists():
                shutil.rmtree(work_root)
        except Exception:
            pass


def main() -> int:
    parser = argparse.ArgumentParser(description="PQ-NAS update apply helper")
    parser.add_argument("--plan-id", required=True)
    parser.add_argument(
        "--updates-root",
        default=os.environ.get("PQNAS_UPDATES_ROOT", str(UPDATES_ROOT_DEFAULT)),
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--validation-only",
        action="store_true",
        help="Validate the plan and package but do not install anything.",
    )
    mode.add_argument(
        "--dry-run",
        action="store_true",
        help="Extract and validate supported update actions without modifying files.",
    )
    mode.add_argument(
        "--apply",
        action="store_true",
        help="Apply supported update actions. Phase 5A supports static_file and core_binary only.",
    )
    args = parser.parse_args()

    try:
        plan_id = validate_plan_id(args.plan_id)
    except ValueError as e:
        return fail("bad_plan_id", str(e))

    updates_root = Path(args.updates_root)

    try:
        plan, package_path, actual_sha = load_and_validate_plan(plan_id, updates_root)
        applicable_actions, reject_actions = applicable_actions_from_plan(plan)
    except FileNotFoundError as e:
        return fail("not_found", str(e), 404)
    except Exception as e:
        return fail("validation_failed", str(e))

    if args.validation_only:
        return run_validation_only(
            plan=plan,
            plan_id=plan_id,
            actual_sha=actual_sha,
            applicable_actions=applicable_actions,
            reject_actions=reject_actions,
        )

    if args.dry_run:
        return run_dry_run(
            plan=plan,
            plan_id=plan_id,
            package_path=package_path,
            actual_sha=actual_sha,
            updates_root=updates_root,
            applicable_actions=applicable_actions,
            reject_actions=reject_actions,
        )

    if args.apply:
        return run_apply(
            plan=plan,
            plan_id=plan_id,
            package_path=package_path,
            actual_sha=actual_sha,
            updates_root=updates_root,
            applicable_actions=applicable_actions,
            reject_actions=reject_actions,
        )

    return fail("bad_mode", "Expected --validation-only, --dry-run, or --apply.")


if __name__ == "__main__":
    sys.exit(main())
