#!/usr/bin/env python3
"""
Non-UI helpers shared by the PQ-NAS installer and its regression tests.

Security: keeping these helpers in a normal importable module lets tests
exercise the production logic directly without compile()/exec() of dynamically
selected source fragments.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
from typing import List, Optional, Protocol, Tuple


class LogLike(Protocol):
    def write(self, message: str) -> object:
        ...


class DiskLike(Protocol):
    path: str
    name: str
    is_system: bool


class InstallStateLike(Protocol):
    disk: Optional[DiskLike]
    backend: str
    mountpoint: str
    install_mode: str


class OriginStateLike(Protocol):
    nginx_enabled: bool
    nginx_hostname: str
    https_enabled: bool
    external_tls_termination: bool


def have_cmd(name: str) -> bool:
    return shutil.which(name) is not None


def apt_install(
        pkgs: List[str],
        log: Optional[LogLike] = None,
) -> None:
    if not have_cmd("apt-get"):
        raise RuntimeError("apt-get not found (unsupported distro).")

    def w(msg: str) -> None:
        if log:
            log.write(msg)
        else:
            print(msg)

    def run_apt_streaming(argv: List[str], label: str) -> None:
        # Security: retain only a bounded tail for error reporting. The full
        # process output is streamed live but must not accumulate in memory.
        max_error_tail_bytes = 64 * 1024
        error_tail = bytearray()
        output_truncated = False

        p = subprocess.Popen(
            argv,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )
        assert p.stdout is not None

        for raw_line in p.stdout:
            line = raw_line.rstrip("\r\n")
            w(line)

            encoded_line = (line + "\n").encode(
                "utf-8",
                errors="replace",
            )
            error_tail.extend(encoded_line)

            if len(error_tail) > max_error_tail_bytes:
                remove_bytes = (
                    len(error_tail) - max_error_tail_bytes
                )
                del error_tail[:remove_bytes]
                output_truncated = True

        rc = p.wait()
        if rc != 0:
            output = error_tail.decode(
                "utf-8",
                errors="replace",
            ).strip()

            if output_truncated:
                output = (
                    "[output truncated; showing last 64 KiB]\n"
                    + output
                )

            raise RuntimeError(
                f"{label} failed with exit code {rc}:\n{output}"
            )

    w("[*] apt-get update …")
    run_apt_streaming(
        ["apt-get", "update", "-y"],
        "apt-get update",
    )

    w(f"[*] apt-get install: {' '.join(pkgs)} …")
    run_apt_streaming(
        ["apt-get", "install", "-y", *pkgs],
        "apt-get install",
    )


def run_cmd(argv: List[str]) -> str:
    p = subprocess.run(
        argv,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if p.returncode != 0:
        raise RuntimeError(
            f"Command failed: {' '.join(argv)}\n"
            f"{(p.stderr or '').strip()}"
        )
    return p.stdout or ""


def dev_part_path(disk: DiskLike) -> str:
    # nvme uses p1, sata uses 1
    return (
        f"{disk.path}p1"
        if "nvme" in disk.name
        else f"{disk.path}1"
    )


def letsencrypt_cert_paths(hostname: str) -> tuple[str, str]:
    """
    Return (fullchain, privkey) paths for a hostname under
    /etc/letsencrypt/live/<host>/.
    """
    host = (hostname or "").strip()
    base = f"/etc/letsencrypt/live/{host}"
    return (
        os.path.join(base, "fullchain.pem"),
        os.path.join(base, "privkey.pem"),
    )


def have_letsencrypt_cert(hostname: str) -> bool:
    """
    True if certbot/letsencrypt cert files exist for hostname.
    """
    fullchain, privkey = letsencrypt_cert_paths(hostname)
    return (
        os.path.isfile(fullchain)
        and os.path.isfile(privkey)
    )


def installer_public_origin(
        st: OriginStateLike,
) -> Optional[str]:
    """
    Resolve the browser/public origin selected by the installer.

    A local nginx listener and the public TLS termination point are separate
    concerns. In particular, external TLS must remain HTTPS publicly even
    though this host serves only HTTP to the upstream reverse proxy.
    """
    host = (st.nginx_hostname or "").strip()
    if not host:
        return None

    if st.external_tls_termination:
        return f"https://{host}"

    if st.nginx_enabled:
        if (
            st.https_enabled
            and have_letsencrypt_cert(host)
        ):
            return f"https://{host}"
        return f"http://{host}"

    # Preserve the existing tunnel/external-proxy behavior when local nginx
    # is disabled. Those deployments expose the configured public host via
    # HTTPS elsewhere.
    return f"https://{host}"


def compute_tls_spki_sha256_pin_from_host(
        hostname: str,
        port: int = 443,
) -> str:
    """
    Best-effort fallback for external HTTPS / Cloudflare / non-local TLS.

    This asks the currently reachable TLS endpoint for its leaf certificate
    and computes the SPKI pin from that.
    """
    host = (hostname or "").strip()
    if not host:
        return ""

    p1 = subprocess.run(
        [
            "openssl",
            "s_client",
            "-connect",
            f"{host}:{port}",
            "-servername",
            host,
        ],
        input=b"",
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        timeout=15,
        check=False,
    )
    if p1.returncode != 0 or not p1.stdout:
        return ""

    p2 = subprocess.run(
        ["openssl", "x509", "-pubkey", "-noout"],
        input=p1.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if p2.returncode != 0 or not p2.stdout:
        return ""

    p3 = subprocess.run(
        ["openssl", "pkey", "-pubin", "-outform", "DER"],
        input=p2.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if p3.returncode != 0 or not p3.stdout:
        return ""

    p4 = subprocess.run(
        ["openssl", "dgst", "-sha256", "-binary"],
        input=p3.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if p4.returncode != 0 or not p4.stdout:
        return ""

    p5 = subprocess.run(
        ["openssl", "base64", "-A"],
        input=p4.stdout,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if p5.returncode != 0 or not p5.stdout:
        return ""

    b64 = (
        (p5.stdout or b"")
        .decode("ascii", errors="replace")
        .strip()
    )
    pin = f"sha256/{b64}"

    if not re.fullmatch(
        r"sha256/[A-Za-z0-9+/]{43}=",
        pin,
    ):
        return ""

    return pin


def detect_external_tls_spki_sha256_pin(
        hostname: str,
        log: Optional[LogLike] = None,
) -> tuple[str, str]:
    """
    Best-effort SPKI pin detection from the public HTTPS endpoint.

    Security: external TLS must use the certificate actually served by the
    public endpoint, never stale local certificate files. A temporary DNS/TLS
    failure must not abort installation; callers receive an empty pin and can
    retry configuration once the public endpoint is reachable.
    """
    host = (hostname or "").strip()
    if not host:
        return ("", "")

    try:
        pin = compute_tls_spki_sha256_pin_from_host(
            host,
            443,
        )
        if pin:
            return (pin, f"{host}:443")
        if log:
            log.write(
                f"[WARN] Public HTTPS endpoint {host}:443 "
                "did not provide a usable TLS SPKI pin."
            )
    except Exception as exc:
        if log:
            log.write(
                "[WARN] Could not compute TLS SPKI pin from "
                f"public endpoint {host}:443: {exc}"
            )

    return ("", "")


def detect_existing_filesystem_info(
        path: str,
) -> tuple[str, str, str]:
    """
    Return (source, filesystem type, backing mount) for an existing path.

    The requested PQ-NAS directory may not exist yet. Walk toward its nearest
    existing parent and ask findmnt about that path.

    Security: this helper is strictly read-only. It does not create paths,
    mount filesystems, modify partition tables, or write /etc/fstab.
    """
    requested = os.path.abspath(
        (path or "/").strip() or "/"
    )
    probe = requested

    while not os.path.exists(probe):
        parent = os.path.dirname(probe)
        if parent == probe:
            break
        probe = parent

    try:
        raw = run_cmd([
            "findmnt",
            "-J",
            "-T",
            probe,
            "-o",
            "SOURCE,FSTYPE,TARGET",
        ])
        payload = json.loads(raw)
        filesystems = payload.get("filesystems") or []
        if not filesystems:
            return ("", "", "")

        fs = filesystems[0]
        source = str(fs.get("source") or "").strip()
        fstype = str(fs.get("fstype") or "").strip()
        target = str(fs.get("target") or "").strip()
        return (source, fstype, target)
    except (
        OSError,
        RuntimeError,
        TypeError,
        ValueError,
    ):
        # Detection is informational only. Failure must never turn existing
        # mode into a destructive or mount-changing fallback.
        return ("", "", "")


def plan_for(
        state: InstallStateLike,
) -> Tuple[List[List[str]], List[str]]:
    """
    Returns: (plan_argvs, notes)
    Plan uses a single-disk model for now.
    """
    assert state.disk, "disk not selected"
    disk = state.disk
    mp = state.mountpoint.rstrip("/") or "/srv/pqnas"

    notes: List[str] = []
    plan: List[List[str]] = []

    if state.install_mode == "existing":
        notes.append(
            "Mode: existing filesystem "
            "(NO wipe, NO partitioning)."
        )
        notes.append(f"PQ-NAS path: {mp}")

        source, fstype, backing_mount = (
            detect_existing_filesystem_info(mp)
        )

        notes.append(
            f"Backing mount: {backing_mount or 'unknown'}"
        )
        notes.append(
            f"Filesystem: {fstype or 'unknown'}"
        )
        notes.append(
            f"Source: {source or 'unknown'}"
        )

        if backing_mount:
            notes.append(
                "WARNING: Existing filesystem capacity is "
                "shared with other data on "
                f"{backing_mount}."
            )
        else:
            notes.append(
                "WARNING: Existing filesystem capacity is "
                "shared with other data on its backing "
                "filesystem."
            )

        return plan, notes

    if disk.is_system:
        raise RuntimeError(
            "Refusing to operate on system disk "
            "(contains / mount)."
        )

    plan += [
        ["wipefs", "-a", disk.path],
        ["sgdisk", "--zap-all", disk.path],
        [
            "dd",
            "if=/dev/zero",
            f"of={disk.path}",
            "bs=1M",
            "count=32",
            "status=none",
        ],
        ["sync"],
        ["sgdisk", "--zap-all", disk.path],
        ["partprobe", disk.path],
    ]
    notes.append(
        "Disk will be destroyed "
        "(wipefs + zap GPT + zero first 32MiB)."
    )

    if state.backend == "ext4":
        part = dev_part_path(disk)
        plan += [
            [
                "parted",
                disk.path,
                "--script",
                "mklabel",
                "gpt",
                "mkpart",
                "primary",
                "ext4",
                "1MiB",
                "100%",
            ],
            ["partprobe", disk.path],
            [
                "mkfs.ext4",
                "-F",
                "-L",
                "PQNAS_DATA",
                part,
            ],
            ["mkdir", "-p", mp],
            ["mount", part, mp],
        ]
        notes.append("Backend: ext4 (no snapshots).")

    elif state.backend == "btrfs":
        part = dev_part_path(disk)
        plan += [
            [
                "parted",
                disk.path,
                "--script",
                "mklabel",
                "gpt",
                "mkpart",
                "primary",
                "btrfs",
                "1MiB",
                "100%",
            ],
            ["partprobe", disk.path],
            [
                "mkfs.btrfs",
                "-f",
                "-L",
                "PQNAS_DATA",
                part,
            ],
            ["mkdir", "-p", mp],
            ["mount", part, mp],
            [
                "btrfs",
                "subvolume",
                "create",
                f"{mp}/data",
            ],
            [
                "btrfs",
                "subvolume",
                "create",
                f"{mp}/snaps",
            ],
            ["umount", mp],
            [
                "mount",
                "-o",
                "subvol=data",
                part,
                mp,
            ],
        ]
        notes.append(
            "Backend: btrfs "
            "(snapshots supported via subvolumes)."
        )

    else:
        raise RuntimeError(
            f"Unknown backend: {state.backend}"
        )

    notes.append("TODO: fstab + permissions.")
    return plan, notes


def installer_health_access_target(
        public_origin: Optional[str],
        server_role: str,
) -> tuple[str, str, str]:
    """
    Return (label, URL, listener note) for the installer Health screen.

    Security: the public scheme comes from the already-resolved installer
    origin rather than being inferred from the local nginx listener.
    """
    origin = (
        (public_origin or "")
        .strip()
        .rstrip("/")
    )
    if origin:
        return (
            "Access URL",
            f"{origin}/",
            "",
        )

    role = (
        (server_role or "standalone")
        .strip()
        .lower()
    )
    if role == "standalone":
        return (
            "Access URL",
            "http://<server-ip>:8081/",
            "Standalone listens directly on port 8081 "
            "when no local nginx proxy is enabled.",
        )

    return (
        "Local service URL",
        "http://127.0.0.1:8081/ (loopback only)",
        "Director and Storage Node roles require a local "
        "reverse proxy or secure tunnel for remote access. "
        "Use TLS when exposing the service outside the host.",
    )
