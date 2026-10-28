#!/usr/bin/env python3
"""Collect redacted installer/cluster smoke-test evidence.

This read-only helper reports fixed-path metadata, validated non-secret cluster
configuration, selected systemd properties, and optional exact-token leak
checks. It never prints file contents, tokens, credentials, raw journal output,
raw process arguments, or raw exception text.
"""

from __future__ import annotations

import argparse
import base64
import errno
import grp
import json
import os
import pwd
import re
import selectors
import stat
import subprocess
import sys
import time
from dataclasses import asdict
from pathlib import Path
from typing import Any, Optional

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "tools" / "installer"))

from pqnas_cluster_config import (  # noqa: E402
    ClusterConfigError,
    parse_cluster_config_from_env_text,
)

MAX_ENV_BYTES = 64 * 1024
MAX_OUTPUT_BYTES = 2 * 1024 * 1024
MAX_PROC_BYTES = 1024 * 1024
MAX_SECRET_BYTES = 128
MAX_PATH_BYTES = 4096
DEFAULT_NODE_CREDENTIAL_FILE = (
    "/var/lib/pqnas/cluster/node-credential"
)
DEFAULT_NODE_BOOTSTRAP_TOKEN_FILE = (
    "/var/lib/pqnas/cluster/bootstrap/enrollment.token"
)
TOKEN_RE = re.compile(rb"[A-Za-z0-9_-]{43}\Z")
ENV_KEY_RE = re.compile(r"[A-Z][A-Z0-9_]*\Z")

FORBIDDEN_ENV_KEYS = frozenset({
    "PQNAS_NODE_BOOTSTRAP_TOKEN",
    "PQNAS_CLUSTER_CREDENTIAL",
})


class CollectorError(RuntimeError):
    def __init__(self, reason: str) -> None:
        super().__init__(reason)
        self.reason = reason


def check(
    check_id: str,
    status_value: str,
    reason: str,
    **details: Any,
) -> dict[str, Any]:
    item: dict[str, Any] = {
        "id": check_id,
        "status": status_value,
        "reason": reason,
    }
    if details:
        item["details"] = details
    return item


def principal_ids(
    owner: Optional[str],
    group: Optional[str],
) -> tuple[Optional[int], Optional[int]]:
    try:
        uid = None if owner is None else pwd.getpwnam(owner).pw_uid
        gid = None if group is None else grp.getgrnam(group).gr_gid
    except KeyError as exc:
        raise CollectorError("expected_principal_missing") from exc
    return uid, gid


def principal_name(value: int, *, user: bool) -> str:
    try:
        return (
            pwd.getpwuid(value).pw_name
            if user
            else grp.getgrgid(value).gr_name
        )
    except KeyError:
        return str(value)


def inspect_path(
    check_id: str,
    path: str,
    *,
    kind: str,
    owner: Optional[str],
    group: Optional[str],
    mode: Optional[int],
    required: bool,
    forbidden: bool = False,
    single_link: bool = False,
    no_group_other_write: bool = False,
) -> dict[str, Any]:
    target = Path(path)
    try:
        meta = target.lstat()
    except FileNotFoundError:
        return check(
            check_id,
            "pass" if forbidden or not required else "fail",
            "not_present" if forbidden or not required else "required_path_missing",
            path=path,
        )
    except OSError:
        return check(check_id, "fail", "metadata_unavailable", path=path)

    file_mode = stat.S_IMODE(meta.st_mode)
    details = {
        "path": path,
        "uid": meta.st_uid,
        "gid": meta.st_gid,
        "owner": principal_name(meta.st_uid, user=True),
        "group": principal_name(meta.st_gid, user=False),
        "mode": f"{file_mode:04o}",
        "nlink": meta.st_nlink,
    }

    if forbidden:
        return check(check_id, "fail", "unexpected_path_present", **details)
    if stat.S_ISLNK(meta.st_mode):
        return check(check_id, "fail", "symlink_rejected", **details)

    kind_ok = (
        kind == "file" and stat.S_ISREG(meta.st_mode)
    ) or (
        kind == "directory" and stat.S_ISDIR(meta.st_mode)
    )
    if not kind_ok:
        return check(check_id, "fail", "unexpected_file_type", **details)

    try:
        expected_uid, expected_gid = principal_ids(owner, group)
    except CollectorError as exc:
        return check(check_id, "fail", exc.reason, **details)

    if expected_uid is not None and meta.st_uid != expected_uid:
        return check(check_id, "fail", "owner_mismatch", **details)
    if expected_gid is not None and meta.st_gid != expected_gid:
        return check(check_id, "fail", "group_mismatch", **details)
    if mode is not None and file_mode != mode:
        return check(
            check_id,
            "fail",
            "mode_mismatch",
            expected_mode=f"{mode:04o}",
            **details,
        )
    if single_link and meta.st_nlink != 1:
        return check(check_id, "fail", "hardlink_rejected", **details)
    if no_group_other_write and file_mode & 0o022:
        return check(check_id, "fail", "group_or_other_writable", **details)

    return check(check_id, "pass", "metadata_ok", **details)


def _normalized_file_target(path: Path) -> tuple[str, str]:
    raw = os.fspath(path)
    if (
        not isinstance(raw, str)
        or not raw
        or len(raw.encode("utf-8")) > MAX_PATH_BYTES
        or "\x00" in raw
        or not os.path.isabs(raw)
    ):
        raise CollectorError("file_path_invalid")

    normalized = os.path.normpath(raw)
    target = Path(normalized)
    if (
        normalized != raw
        or normalized == "/"
        or not target.name
    ):
        raise CollectorError("file_path_invalid")

    for component in target.parts:
        if component in (".", ".."):
            raise CollectorError("file_path_invalid")

    return str(target.parent), target.name


def _read_descriptor_bounded(
    descriptor: int,
    limit: int,
) -> bytearray:
    if limit < 1:
        raise CollectorError("file_size_invalid")

    output = bytearray()
    try:
        while True:
            try:
                chunk = os.read(
                    descriptor,
                    min(65536, limit + 1 - len(output)),
                )
            except InterruptedError:
                continue
            except OSError as exc:
                raise CollectorError("file_read_failed") from exc

            if not chunk:
                return output

            output.extend(chunk)
            if len(output) > limit:
                raise CollectorError("file_too_large")
    except Exception:
        # Security: raw env, journal and process data may contain secrets.
        # Overwrite mutable buffers before propagating any failure.
        wipe_secret(output)
        raise


def read_private_file_bounded(
    path: Path,
    limit: int,
    *,
    owner_uid: int,
    owner_gid: int,
    mode: int,
) -> bytearray:
    parent, filename = _normalized_file_target(path)
    directory_flags = (
        os.O_RDONLY
        | os.O_CLOEXEC
        | os.O_DIRECTORY
        | getattr(os, "O_NOFOLLOW", 0)
    )

    try:
        directory_fd = os.open(parent, directory_flags)
    except FileNotFoundError as exc:
        raise CollectorError("file_missing") from exc
    except OSError as exc:
        if exc.errno == errno.ELOOP:
            raise CollectorError("parent_symlink_rejected") from exc
        raise CollectorError("parent_open_failed") from exc

    file_fd = -1
    try:
        parent_meta = os.fstat(directory_fd)
        if (
            not stat.S_ISDIR(parent_meta.st_mode)
            or parent_meta.st_uid != owner_uid
            or parent_meta.st_gid != owner_gid
            or stat.S_IMODE(parent_meta.st_mode) & 0o022
        ):
            raise CollectorError("parent_metadata_unsafe")

        flags = (
            os.O_RDONLY
            | os.O_CLOEXEC
            | getattr(os, "O_NOFOLLOW", 0)
        )
        try:
            file_fd = os.open(
                filename,
                flags,
                dir_fd=directory_fd,
            )
        except FileNotFoundError as exc:
            raise CollectorError("file_missing") from exc
        except OSError as exc:
            if exc.errno == errno.ELOOP:
                raise CollectorError("symlink_rejected") from exc
            raise CollectorError("file_open_failed") from exc

        metadata = os.fstat(file_fd)
        if not stat.S_ISREG(metadata.st_mode):
            raise CollectorError("not_regular_file")
        if metadata.st_nlink != 1:
            raise CollectorError("hardlink_rejected")
        if (
            metadata.st_uid != owner_uid
            or metadata.st_gid != owner_gid
            or stat.S_IMODE(metadata.st_mode) != mode
        ):
            raise CollectorError("file_metadata_unsafe")
        if metadata.st_size < 0 or metadata.st_size > limit:
            raise CollectorError("file_size_invalid")

        return _read_descriptor_bounded(file_fd, limit)
    finally:
        if file_fd >= 0:
            os.close(file_fd)
        os.close(directory_fd)


def read_proc_file_bounded(
    path: Path,
    limit: int,
) -> bytearray:
    flags = (
        os.O_RDONLY
        | os.O_CLOEXEC
        | getattr(os, "O_NOFOLLOW", 0)
    )
    try:
        descriptor = os.open(path, flags)
    except FileNotFoundError as exc:
        raise CollectorError("file_missing") from exc
    except OSError as exc:
        if exc.errno == errno.ELOOP:
            raise CollectorError("symlink_rejected") from exc
        raise CollectorError("file_open_failed") from exc

    try:
        metadata = os.fstat(descriptor)
        if not stat.S_ISREG(metadata.st_mode):
            raise CollectorError("not_regular_file")
        if metadata.st_size < 0 or metadata.st_size > limit:
            raise CollectorError("file_size_invalid")
        return _read_descriptor_bounded(descriptor, limit)
    finally:
        os.close(descriptor)

def env_key_names(text: str) -> list[str]:
    keys: list[str] = []
    for line in text.splitlines():
        if not line or line.lstrip().startswith("#"):
            continue
        if "=" not in line:
            raise CollectorError("env_line_malformed")
        key, _value = line.split("=", 1)
        if not ENV_KEY_RE.fullmatch(key):
            raise CollectorError("env_key_malformed")
        keys.append(key)
    return sorted(set(keys))


def summarize_env(
    contents: bytes | bytearray,
) -> dict[str, Any]:
    try:
        text = contents.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise CollectorError("env_not_utf8") from exc
    if "\x00" in text:
        raise CollectorError("env_contains_nul")

    keys = env_key_names(text)
    try:
        config = asdict(parse_cluster_config_from_env_text(text))
    except ClusterConfigError as exc:
        raise CollectorError("cluster_config_invalid") from exc

    return {
        "keys": keys,
        "cluster": {
            "PQNAS_SERVER_ROLE": config["role"],
            "PQNAS_NODE_ID": config["node_id"],
            "PQNAS_DIRECTOR_URL": config["director_url"],
            "PQNAS_CLUSTER_CREDENTIAL_FILE": config["cluster_credential_file"],
            "PQNAS_NODE_ENROLLMENT_ID": config["enrollment_id"],
            "PQNAS_NODE_ENROLLMENT_GENERATION": config[
                "enrollment_generation"
            ],
            "PQNAS_NODE_API_BASE_URL": config["node_api_base_url"],
            "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE": config[
                "bootstrap_token_file"
            ],
        },
        "forbidden_keys": sorted(set(keys) & FORBIDDEN_ENV_KEYS),
    }


def validate_secret_input(
    raw: bytes | bytearray,
) -> bytearray:
    value = (
        raw
        if isinstance(raw, bytearray)
        else bytearray(raw)
    )

    if len(value) > MAX_SECRET_BYTES:
        wipe_secret(value)
        raise CollectorError("secret_input_too_large")

    while value and value[-1] in (ord("\r"), ord("\n")):
        value.pop()

    if len(value) != 43 or not TOKEN_RE.fullmatch(value):
        wipe_secret(value)
        raise CollectorError("secret_input_invalid_format")

    try:
        decoded = base64.b64decode(
            bytes(value) + b"=",
            altchars=b"-_",
            validate=True,
        )
    except Exception as exc:
        wipe_secret(value)
        raise CollectorError("secret_input_invalid_encoding") from exc

    if (
        len(decoded) != 32
        or base64.urlsafe_b64encode(decoded).rstrip(b"=")
            != value
    ):
        wipe_secret(value)
        raise CollectorError("secret_input_noncanonical")

    return value

def wipe_secret(secret: Optional[bytearray]) -> None:
    if secret is not None:
        for index in range(len(secret)):
            secret[index] = 0


def secret_result(
    check_id: str,
    found: bool,
    **details: Any,
) -> dict[str, Any]:
    return check(
        check_id,
        "fail" if found else "pass",
        "secret_found" if found else "secret_not_found",
        **details,
    )


def run_bounded(
    argv: list[str],
    *,
    timeout: float = 8.0,
    limit: int = MAX_OUTPUT_BYTES,
) -> tuple[int, bytearray]:
    try:
        process = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            close_fds=True,
        )
    except OSError as exc:
        raise CollectorError("command_start_failed") from exc

    if process.stdout is None:
        process.kill()
        process.wait()
        raise CollectorError("command_pipe_failed")

    selector = selectors.DefaultSelector()
    output = bytearray()
    deadline = time.monotonic() + timeout

    try:
        selector.register(process.stdout, selectors.EVENT_READ)

        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise CollectorError("command_timeout")

            events = selector.select(remaining)
            if not events:
                if process.poll() is not None:
                    break
                continue

            chunk = os.read(
                process.stdout.fileno(),
                min(65536, limit + 1 - len(output)),
            )
            if not chunk:
                break

            output.extend(chunk)
            if len(output) > limit:
                raise CollectorError("command_output_too_large")

        remaining = max(0.1, deadline - time.monotonic())
        try:
            return_code = process.wait(timeout=remaining)
        except subprocess.TimeoutExpired as exc:
            raise CollectorError("command_timeout") from exc

        return return_code, output
    except Exception:
        # Security: command output can contain journal or service data.
        wipe_secret(output)
        raise
    finally:
        selector.close()
        process.stdout.close()
        if process.poll() is None:
            process.kill()
            process.wait()

def systemctl_value(name: str) -> str:
    output: Optional[bytearray] = None
    try:
        return_code, output = run_bounded([
            "systemctl",
            "show",
            "pqnas.service",
            f"--property={name}",
            "--value",
            "--no-pager",
        ])
        if return_code != 0:
            raise CollectorError("systemctl_query_failed")

        try:
            return output.decode("utf-8").strip()
        except UnicodeDecodeError as exc:
            raise CollectorError("systemctl_output_invalid") from exc
    finally:
        wipe_secret(output)

def systemd_checks(skip: bool) -> list[dict[str, Any]]:
    if skip:
        return [check("systemd", "warn", "systemd_checks_skipped")]

    output: list[dict[str, Any]] = []
    for name, expected in (
        ("ActiveState", "active"),
        ("User", "pqnas"),
        ("Group", "pqnas"),
        ("ProtectSystem", "strict"),
    ):
        try:
            actual = systemctl_value(name)
            ok = actual == expected
            output.append(check(
                f"systemd.{name}",
                "pass" if ok else "fail",
                "property_ok" if ok else "property_mismatch",
                actual=actual,
                expected=expected,
            ))
        except CollectorError as exc:
            output.append(check(f"systemd.{name}", "fail", exc.reason))

    try:
        paths = systemctl_value("ReadWritePaths").split()
        unsafe = any(
            value == "/etc/pqnas" or value.startswith("/etc/pqnas/")
            for value in paths
        )
        output.append(check(
            "systemd.ReadWritePaths",
            "fail" if unsafe else "pass",
            "etc_pqnas_writable" if unsafe else "etc_pqnas_not_writable",
            entry_count=len(paths),
        ))
    except CollectorError as exc:
        output.append(check("systemd.ReadWritePaths", "fail", exc.reason))

    try:
        files = systemctl_value("EnvironmentFiles")
        ok = (
            "/etc/pqnas/pqnas.env" in files
            and "/etc/pqnas/keys.env" in files
        )
        output.append(check(
            "systemd.EnvironmentFiles",
            "pass" if ok else "fail",
            "environment_files_ok" if ok else "environment_files_missing",
        ))
    except CollectorError as exc:
        output.append(check("systemd.EnvironmentFiles", "fail", exc.reason))
    return output


def process_secret_checks(
    secret: bytearray,
) -> list[dict[str, Any]]:
    states: dict[str, dict[str, Any]] = {
        "command": {
            "found": False,
            "checked": 0,
            "unreadable": 0,
            "vanished": 0,
        },
        "environment": {
            "found": False,
            "checked": 0,
            "unreadable": 0,
            "vanished": 0,
        },
    }

    try:
        processes = sorted(
            (
                path for path in Path("/proc").iterdir()
                if path.name.isdigit()
            ),
            key=lambda path: int(path.name),
        )
    except OSError:
        return [check("secret.processes", "warn", "proc_unavailable")]

    for process in processes:
        for name, destination in (
            ("cmdline", "command"),
            ("environ", "environment"),
        ):
            data: Optional[bytearray] = None
            try:
                data = read_proc_file_bounded(
                    process / name,
                    MAX_PROC_BYTES,
                )
                states[destination]["checked"] += 1
                states[destination]["found"] |= (
                    data.find(secret) >= 0
                )
            except CollectorError as exc:
                if exc.reason == "file_missing":
                    states[destination]["vanished"] += 1
                else:
                    states[destination]["unreadable"] += 1
            finally:
                # Security: process argv/environment can contain credentials.
                wipe_secret(data)

    output: list[dict[str, Any]] = []
    for destination, check_id in (
        ("command", "secret.process_cmdline"),
        ("environment", "secret.process_environment"),
    ):
        state = states[destination]
        details = {
            "entries_checked": state["checked"],
            "unreadable_entries": state["unreadable"],
            "vanished_entries": state["vanished"],
        }

        if state["found"]:
            output.append(secret_result(
                check_id,
                True,
                **details,
            ))
        elif state["unreadable"]:
            output.append(check(
                check_id,
                "warn",
                "secret_scan_incomplete",
                **details,
            ))
        else:
            output.append(secret_result(
                check_id,
                False,
                **details,
            ))

    return output


def leak_checks(
    secret: bytearray,
    pqnas_env: Optional[bytearray],
    *,
    skip_systemd: bool,
) -> list[dict[str, Any]]:
    output = [
        (
            check("secret.pqnas_env", "warn", "pqnas_env_unavailable")
            if pqnas_env is None
            else secret_result(
                "secret.pqnas_env",
                pqnas_env.find(secret) >= 0,
            )
        )
    ]

    if skip_systemd:
        output.append(check(
            "secret.journal",
            "warn",
            "journal_check_skipped",
        ))
    else:
        journal: Optional[bytearray] = None
        try:
            return_code, journal = run_bounded([
                "journalctl",
                "-u",
                "pqnas.service",
                "--no-pager",
                "--output=cat",
                "--lines=500",
            ])
            if return_code != 0:
                output.append(check(
                    "secret.journal",
                    "warn",
                    "journal_query_failed",
                    lines_requested=500,
                ))
            else:
                output.append(secret_result(
                    "secret.journal",
                    journal.find(secret) >= 0,
                    lines_requested=500,
                ))
        except CollectorError as exc:
            output.append(check(
                "secret.journal",
                "warn",
                exc.reason,
            ))
        finally:
            # Security: journal output can contain the token being searched.
            wipe_secret(journal)

    output.extend(process_secret_checks(secret))
    return output

def role_for_phase(phase: str) -> Optional[str]:
    return {
        "standalone": "standalone",
        "director": "director",
        "node-before-enrollment": "node",
        "node-enrolled": "node",
    }.get(phase)


def filesystem_checks(
    phase: str,
    cluster: Optional[dict[str, Any]],
) -> list[dict[str, Any]]:
    common_specs = (
        ("path.etc_pqnas", "/etc/pqnas", "directory", "root", "root", None, True, False, False, True),
        ("path.pqnas_env", "/etc/pqnas/pqnas.env", "file", "root", "root", 0o600, True, False, True, False),
        ("path.keys_env", "/etc/pqnas/keys.env", "file", "root", "root", 0o600, True, False, True, False),
        ("path.srv_pqnas", "/srv/pqnas", "directory", "pqnas", "pqnas", 0o750, True, False, False, False),
        ("path.config", "/srv/pqnas/config", "directory", "pqnas", "pqnas", 0o750, True, False, False, False),
        ("path.data", "/srv/pqnas/data", "directory", "pqnas", "pqnas", 0o750, True, False, False, False),
        ("path.var_lib_pqnas", "/var/lib/pqnas", "directory", "pqnas", "pqnas", 0o750, True, False, False, False),
    )

    output = [
        inspect_path(
            check_id,
            path,
            kind=kind,
            owner=owner,
            group=group,
            mode=mode,
            required=required,
            forbidden=forbidden,
            single_link=single_link,
            no_group_other_write=no_write,
        )
        for (
            check_id,
            path,
            kind,
            owner,
            group,
            mode,
            required,
            forbidden,
            single_link,
            no_write,
        ) in common_specs
    ]

    actual_role = (
        None
        if cluster is None
        else cluster.get("PQNAS_SERVER_ROLE")
    )
    node_phase = phase.startswith("node-")
    inspect_node_paths = (
        node_phase
        or (
            phase == "inventory"
            and actual_role == "node"
        )
    )

    if inspect_node_paths:
        if cluster is None or actual_role != "node":
            output.append(check(
                "path.node_secrets",
                "fail",
                "node_config_unavailable",
            ))
            return output

        credential_path = str(
            cluster["PQNAS_CLUSTER_CREDENTIAL_FILE"]
        )
        token_path = str(
            cluster["PQNAS_NODE_BOOTSTRAP_TOKEN_FILE"]
        )

        for check_id, parent in (
            (
                "path.node_credential_parent",
                str(Path(credential_path).parent),
            ),
            (
                "path.bootstrap_token_parent",
                str(Path(token_path).parent),
            ),
        ):
            output.append(inspect_path(
                check_id,
                parent,
                kind="directory",
                owner="pqnas",
                group="pqnas",
                mode=None,
                required=True,
                no_group_other_write=True,
            ))

        token_required = phase == "node-before-enrollment"
        token_forbidden = phase == "node-enrolled"
        credential_required = phase == "node-enrolled"
        credential_forbidden = phase == "node-before-enrollment"

        output.append(inspect_path(
            "path.bootstrap_token",
            token_path,
            kind="file",
            owner="pqnas",
            group="pqnas",
            mode=0o600,
            required=token_required,
            forbidden=token_forbidden,
            single_link=True,
        ))
        output.append(inspect_path(
            "path.node_credential",
            credential_path,
            kind="file",
            owner="pqnas",
            group="pqnas",
            mode=0o600,
            required=credential_required,
            forbidden=credential_forbidden,
            single_link=True,
        ))
        return output

    token_forbidden = (
        phase in ("standalone", "director")
        or (
            phase == "inventory"
            and actual_role in ("standalone", "director")
        )
    )
    output.append(inspect_path(
        "path.bootstrap_token",
        DEFAULT_NODE_BOOTSTRAP_TOKEN_FILE,
        kind="file",
        owner="pqnas",
        group="pqnas",
        mode=0o600,
        required=False,
        forbidden=token_forbidden,
        single_link=True,
    ))
    return output

def overall(checks: list[dict[str, Any]]) -> str:
    statuses = {item["status"] for item in checks}
    if "fail" in statuses:
        return "fail"
    if "warn" in statuses:
        return "warn"
    return "pass"


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Collect redacted installer/cluster smoke evidence."
    )
    parser.add_argument(
        "--phase",
        choices=(
            "inventory",
            "standalone",
            "director",
            "node-before-enrollment",
            "node-enrolled",
        ),
        default="inventory",
    )
    parser.add_argument("--no-systemd", action="store_true")
    parser.add_argument(
        "--secret-stdin",
        action="store_true",
        help="Read one canonical bootstrap token from stdin for exact leak checks.",
    )
    return parser.parse_args()


def build_report(args: argparse.Namespace) -> dict[str, Any]:
    checks: list[dict[str, Any]] = []
    env_contents: Optional[bytearray] = None
    summary: Optional[dict[str, Any]] = None
    secret: Optional[bytearray] = None

    try:
        try:
            env_contents = read_private_file_bounded(
                Path("/etc/pqnas/pqnas.env"),
                MAX_ENV_BYTES,
                owner_uid=0,
                owner_gid=0,
                mode=0o600,
            )
            summary = summarize_env(env_contents)
            forbidden = summary["forbidden_keys"]
            checks.append(check(
                "config.pqnas_env",
                "fail" if forbidden else "pass",
                (
                    "forbidden_cluster_secret_key_present"
                    if forbidden
                    else "config_valid"
                ),
                keys=summary["keys"],
                cluster=summary["cluster"],
                forbidden_keys=forbidden,
            ))

            expected = role_for_phase(args.phase)
            if expected is not None:
                actual = summary["cluster"]["PQNAS_SERVER_ROLE"]
                checks.append(check(
                    "config.role",
                    "pass" if actual == expected else "fail",
                    "role_ok" if actual == expected else "role_mismatch",
                    actual=actual,
                    expected=expected,
                ))
        except CollectorError as exc:
            checks.append(check(
                "config.pqnas_env",
                "fail",
                exc.reason,
            ))

        checks.extend(filesystem_checks(
            args.phase,
            (
                None
                if summary is None
                else summary["cluster"]
            ),
        ))
        checks.extend(systemd_checks(args.no_systemd))

        if args.secret_stdin:
            secret = validate_secret_input(
                bytearray(
                    sys.stdin.buffer.read(
                        MAX_SECRET_BYTES + 1
                    )
                )
            )
            checks.extend(leak_checks(
                secret,
                env_contents,
                skip_systemd=args.no_systemd,
            ))

        return {
            "schema_version": 1,
            "collector": "installer_cluster_smoke_evidence",
            "phase": args.phase,
            "overall": overall(checks),
            "checks": checks,
        }
    finally:
        # Security: pqnas.env and exact-token buffers can contain secrets.
        wipe_secret(secret)
        wipe_secret(env_contents)

def main() -> int:
    try:
        report = build_report(arguments())
    except CollectorError as exc:
        report = {
            "schema_version": 1,
            "collector": "installer_cluster_smoke_evidence",
            "overall": "fail",
            "checks": [check("collector", "fail", exc.reason)],
        }
    except Exception:
        # Security: use a stable reason code. Raw exceptions can contain paths,
        # command output, or other details that must not enter evidence files.
        report = {
            "schema_version": 1,
            "collector": "installer_cluster_smoke_evidence",
            "overall": "fail",
            "checks": [
                check(
                    "collector",
                    "fail",
                    "unexpected_collector_failure",
                )
            ],
        }

    print(json.dumps(report, indent=2, sort_keys=True))
    return 0 if report["overall"] != "fail" else 2


if __name__ == "__main__":
    raise SystemExit(main())
