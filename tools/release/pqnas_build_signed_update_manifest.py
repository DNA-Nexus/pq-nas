#!/usr/bin/env python3
"""
Build and sign PQ-NAS update package manifest.

Security:
- The signed manifest binds package files and supported update actions.
- The private signing key stays on the release machine only.
- The customer system later verifies this manifest before root applies core updates.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import stat
import subprocess
import tempfile
from datetime import datetime, timezone
from pathlib import Path


MANIFEST_NAME = "pqnas-update-manifest.v1.json"
SIGNATURE_NAME = "pqnas-update-manifest.v1.sig"


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def mode_octal(path: Path) -> str:
    return f"{stat.S_IMODE(path.stat().st_mode):04o}"


def posix_rel(path: Path, root: Path) -> str:
    return path.relative_to(root).as_posix()


def is_manifest_artifact(rel: str) -> bool:
    return rel in (MANIFEST_NAME, SIGNATURE_NAME)


def build_file_entries(stage: Path) -> list[dict]:
    files: list[dict] = []

    for path in sorted(stage.rglob("*")):
        if not path.is_file():
            continue

        rel = posix_rel(path, stage)
        if is_manifest_artifact(rel):
            continue

        st = path.stat()
        files.append({
            "path": rel,
            "size": st.st_size,
            "sha256": sha256_file(path),
            "mode": mode_octal(path),
        })

    return files


def build_actions(files: list[dict]) -> list[dict]:
    actions: list[dict] = []

    for f in files:
        src = str(f["path"])

        if src == "pqnas_server" or src.endswith("/pqnas_server"):
            actions.append({
                "type": "core_binary",
                "action": "update",
                "source": src,
                "target": "/usr/local/bin/pqnas_server",
                "sha256": f["sha256"],
                "mode": "0755",
            })
            continue

        if src.startswith("static/"):
            rel = src[len("static/"):]
            if not rel:
                continue

            actions.append({
                "type": "static_file",
                "action": "update",
                "source": src,
                "target": "/opt/pqnas/static/" + rel,
                "sha256": f["sha256"],
                "mode": "0644",
            })

    return actions


def write_json(path: Path, obj: dict) -> None:
    path.write_text(
        json.dumps(obj, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def run_checked(args: list[str]) -> None:
    subprocess.run(args, check=True)


def sign_manifest(openssl: str, private_key: Path, manifest: Path, signature: Path) -> None:
    # Security: use OpenSSL Ed25519 signing without shell=True.
    run_checked([
        openssl,
        "pkeyutl",
        "-sign",
        "-inkey",
        str(private_key),
        "-rawin",
        "-in",
        str(manifest),
        "-out",
        str(signature),
    ])

    with tempfile.TemporaryDirectory(prefix="pqnas-sign-verify-") as td:
        pub = Path(td) / "release.pub"

        run_checked([
            openssl,
            "pkey",
            "-in",
            str(private_key),
            "-pubout",
            "-out",
            str(pub),
        ])

        # Security: fail release build if the just-written signature cannot be verified.
        run_checked([
            openssl,
            "pkeyutl",
            "-verify",
            "-pubin",
            "-inkey",
            str(pub),
            "-rawin",
            "-in",
            str(manifest),
            "-sigfile",
            str(signature),
        ])


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--stage", required=True)
    ap.add_argument("--version", required=True)
    ap.add_argument("--arch", required=True)
    ap.add_argument("--key-id", required=True)
    ap.add_argument("--private-key", required=True)
    ap.add_argument("--openssl", default="openssl")
    ap.add_argument("--out-manifest", required=True)
    ap.add_argument("--out-signature", required=True)
    args = ap.parse_args()

    stage = Path(args.stage).resolve()
    private_key = Path(args.private_key).resolve()
    manifest_path = Path(args.out_manifest).resolve()
    signature_path = Path(args.out_signature).resolve()

    if not stage.is_dir():
        raise SystemExit(f"ERROR: stage is not a directory: {stage}")

    if not private_key.is_file():
        raise SystemExit(f"ERROR: private key not found: {private_key}")

    files = build_file_entries(stage)
    actions = build_actions(files)

    manifest = {
        "manifest_version": 1,
        "kind": "pqnas_signed_update_manifest",
        "product": "pqnas",
        "package_version": args.version,
        "arch": args.arch,
        "created_at": datetime.now(timezone.utc).replace(microsecond=0).isoformat(),
        "signature_algorithm": "Ed25519",
        "signing_key_id": args.key_id,
        "files": files,
        "actions": actions,
    }

    write_json(manifest_path, manifest)
    manifest_path.chmod(0o644)

    sign_manifest(args.openssl, private_key, manifest_path, signature_path)
    signature_path.chmod(0o644)

    print(f"wrote {manifest_path}")
    print(f"wrote {signature_path}")
    print(f"files: {len(files)}")
    print(f"actions: {len(actions)}")
    print(f"core_binary actions: {sum(1 for a in actions if a.get('type') == 'core_binary')}")
    print(f"static_file actions: {sum(1 for a in actions if a.get('type') == 'static_file')}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
