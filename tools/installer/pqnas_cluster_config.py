#!/usr/bin/env python3
# Validated non-secret cluster configuration for the installer.

from __future__ import annotations

import os
import re
from dataclasses import dataclass
from typing import Dict, FrozenSet, List, Mapping, Optional

from pqnas_secure_files import (
    SecurePrivateFileError,
    read_private_file_text,
)

MAX_ENV_FILE_BYTES = 64 * 1024
MAX_PATH_BYTES = 4096
MAX_HTTPS_BASE_URL_BYTES = 2048
MAX_NODE_ID_BYTES = 64
MAX_ENROLLMENT_ID_BYTES = 128
MAX_GENERATION = (1 << 64) - 1

MANAGED_CLUSTER_ENV_KEYS: FrozenSet[str] = frozenset({
    "PQNAS_SERVER_ROLE",
    "PQNAS_NODE_ID",
    "PQNAS_DIRECTOR_URL",
    "PQNAS_CLUSTER_CREDENTIAL_FILE",
    "PQNAS_NODE_ENROLLMENT_ID",
    "PQNAS_NODE_ENROLLMENT_GENERATION",
    "PQNAS_NODE_API_BASE_URL",
    "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE",
})

_NODE_ID_RE = re.compile(
    r"[A-Za-z0-9][A-Za-z0-9._-]{0,63}\Z"
)
_ENROLLMENT_ID_RE = re.compile(
    r"[A-Za-z0-9_-]{1,128}\Z"
)


class ClusterConfigError(ValueError):
    pass


@dataclass(frozen=True)
class ClusterInstallConfig:
    role: str = "standalone"
    node_id: str = ""
    director_url: str = ""
    cluster_credential_file: str = ""
    enrollment_id: str = ""
    enrollment_generation: int = 0
    node_api_base_url: str = ""
    bootstrap_token_file: str = ""


def normalize_cluster_https_base_url(raw: str) -> str:
    if (
        not isinstance(raw, str)
        or not raw
        or len(raw.encode("utf-8")) >
            MAX_HTTPS_BASE_URL_BYTES
    ):
        raise ClusterConfigError(
            "cluster HTTPS base URL has invalid size"
        )

    # Security: mirror the server's strict printable-ASCII authority parsing.
    # Whitespace and non-ASCII input must not be normalized silently.
    for character in raw:
        codepoint = ord(character)
        if codepoint < 0x21 or codepoint > 0x7E:
            raise ClusterConfigError(
                "cluster HTTPS base URL contains invalid characters"
            )

    scheme = "https://"
    if not raw.startswith(scheme):
        raise ClusterConfigError(
            "cluster base URL must use HTTPS"
        )

    candidate = raw[:-1] if raw.endswith("/") else raw
    authority = candidate[len(scheme):]

    if (
        not authority
        or authority.startswith(":")
        or authority.endswith(":")
        or any(
            delimiter in authority
            for delimiter in (
                "/",
                "\\",
                "@",
                "?",
                "#",
                "%",
            )
        )
    ):
        raise ClusterConfigError(
            "cluster URL must be a base HTTPS authority"
        )

    return candidate


def normalize_absolute_file_path(
    raw: str,
    label: str,
) -> str:
    if (
        not isinstance(raw, str)
        or not raw
        or len(raw.encode("utf-8")) > MAX_PATH_BYTES
        or "\x00" in raw
        or not os.path.isabs(raw)
    ):
        raise ClusterConfigError(
            f"{label} must be a bounded absolute path"
        )

    # Security: reject repeated separators and dot components before
    # normalization so the same path cannot have multiple textual forms.
    components = raw.split("/")
    if (
        raw.startswith("//")
        or any(
            component in ("", ".", "..")
            for component in components[1:]
        )
    ):
        raise ClusterConfigError(
            f"{label} must be a normalized path without dot components"
        )

    normalized = os.path.normpath(raw)
    if (
        normalized != raw
        or normalized == "/"
        or not os.path.basename(normalized)
    ):
        raise ClusterConfigError(
            f"{label} must name a normalized file"
        )

    return normalized


def validate_cluster_config(
    config: ClusterInstallConfig,
) -> ClusterInstallConfig:
    if not isinstance(config, ClusterInstallConfig):
        raise ClusterConfigError(
            "invalid cluster configuration object"
        )

    role = config.role.strip().lower()
    if role not in (
        "standalone",
        "director",
        "node",
    ):
        raise ClusterConfigError(
            "server role must be standalone, director, or node"
        )

    node_values = (
        config.node_id,
        config.director_url,
        config.cluster_credential_file,
        config.enrollment_id,
        config.node_api_base_url,
        config.bootstrap_token_file,
    )

    if role == "standalone":
        if (
            any(node_values)
            or config.enrollment_generation != 0
        ):
            raise ClusterConfigError(
                "standalone role must not define cluster node settings"
            )

        return ClusterInstallConfig(
            role="standalone"
        )

    if role == "director":
        if (
            any(node_values)
            or config.enrollment_generation != 0
        ):
            raise ClusterConfigError(
                "director role must not define storage-node settings"
            )

        return ClusterInstallConfig(
            role="director"
        )

    if (
        not isinstance(config.node_id, str)
        or len(config.node_id.encode("utf-8")) >
            MAX_NODE_ID_BYTES
        or not _NODE_ID_RE.fullmatch(config.node_id)
    ):
        raise ClusterConfigError(
            "invalid storage-node identifier"
        )

    if (
        not isinstance(config.enrollment_id, str)
        or len(config.enrollment_id.encode("utf-8")) >
            MAX_ENROLLMENT_ID_BYTES
        or not _ENROLLMENT_ID_RE.fullmatch(
            config.enrollment_id
        )
    ):
        raise ClusterConfigError(
            "invalid enrollment identifier"
        )

    if (
        not isinstance(
            config.enrollment_generation,
            int,
        )
        or isinstance(
            config.enrollment_generation,
            bool,
        )
        or config.enrollment_generation < 1
        or config.enrollment_generation >
            MAX_GENERATION
    ):
        raise ClusterConfigError(
            "invalid enrollment generation"
        )

    director_url = normalize_cluster_https_base_url(
        config.director_url
    )
    node_api_base_url = normalize_cluster_https_base_url(
        config.node_api_base_url
    )
    credential_file = normalize_absolute_file_path(
        config.cluster_credential_file,
        "cluster credential file",
    )
    bootstrap_token_file = normalize_absolute_file_path(
        config.bootstrap_token_file,
        "bootstrap token file",
    )

    if credential_file == bootstrap_token_file:
        raise ClusterConfigError(
            "credential and bootstrap token files must differ"
        )

    return ClusterInstallConfig(
        role="node",
        node_id=config.node_id,
        director_url=director_url,
        cluster_credential_file=credential_file,
        enrollment_id=config.enrollment_id,
        enrollment_generation=(
            config.enrollment_generation
        ),
        node_api_base_url=node_api_base_url,
        bootstrap_token_file=bootstrap_token_file,
    )


def cluster_config_from_mapping(
    values: Mapping[str, str],
) -> ClusterInstallConfig:
    generation_text = values.get(
        "PQNAS_NODE_ENROLLMENT_GENERATION",
        "",
    )

    generation = 0
    if generation_text:
        if (
            len(generation_text) > 20
            or not generation_text.isascii()
            or not generation_text.isdigit()
        ):
            raise ClusterConfigError(
                "invalid enrollment generation"
            )

        generation = int(
            generation_text,
            10,
        )

    return validate_cluster_config(
        ClusterInstallConfig(
            role=values.get(
                "PQNAS_SERVER_ROLE",
                "standalone",
            ),
            node_id=values.get(
                "PQNAS_NODE_ID",
                "",
            ),
            director_url=values.get(
                "PQNAS_DIRECTOR_URL",
                "",
            ),
            cluster_credential_file=values.get(
                "PQNAS_CLUSTER_CREDENTIAL_FILE",
                "",
            ),
            enrollment_id=values.get(
                "PQNAS_NODE_ENROLLMENT_ID",
                "",
            ),
            enrollment_generation=generation,
            node_api_base_url=values.get(
                "PQNAS_NODE_API_BASE_URL",
                "",
            ),
            bootstrap_token_file=values.get(
                "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE",
                "",
            ),
        )
    )


def parse_cluster_config_from_env_text(
    text: str,
) -> ClusterInstallConfig:
    if (
        not isinstance(text, str)
        or len(text.encode("utf-8")) >
            MAX_ENV_FILE_BYTES
        or "\x00" in text
    ):
        raise ClusterConfigError(
            "pqnas.env has invalid size or contents"
        )

    values: Dict[str, str] = {}

    for line_number, raw_line in enumerate(
        text.splitlines(),
        start=1,
    ):
        if (
            not raw_line
            or raw_line.lstrip().startswith("#")
        ):
            continue

        if "=" not in raw_line:
            continue

        raw_key, value = raw_line.split(
            "=",
            1,
        )
        key = raw_key.strip()

        if key not in MANAGED_CLUSTER_ENV_KEYS:
            continue

        if (
            raw_key != key
            or value != value.strip()
        ):
            raise ClusterConfigError(
                "cluster setting contains surrounding whitespace "
                f"on line {line_number}"
            )

        if key in values:
            raise ClusterConfigError(
                "duplicate cluster setting "
                f"on line {line_number}"
            )

        values[key] = value

    return cluster_config_from_mapping(
        values
    )


def load_existing_cluster_config(
    path: str = "/etc/pqnas/pqnas.env",
    *,
    owner_uid: int = 0,
    owner_gid: int = 0,
) -> ClusterInstallConfig:
    try:
        text = read_private_file_text(
            path,
            owner_uid=owner_uid,
            owner_gid=owner_gid,
        )
    except SecurePrivateFileError as exc:
        raise ClusterConfigError(
            "existing pqnas.env could not be read safely"
        ) from exc

    if text is None:
        return ClusterInstallConfig()

    return parse_cluster_config_from_env_text(
        text
    )


def render_cluster_env_lines(
    config: ClusterInstallConfig,
) -> List[str]:
    normalized = validate_cluster_config(
        config
    )

    lines = [
        "",
        "# Cluster role and control-plane settings.",
        f"PQNAS_SERVER_ROLE={normalized.role}",
    ]

    if normalized.role != "node":
        return lines

    lines.extend([
        f"PQNAS_NODE_ID={normalized.node_id}",
        f"PQNAS_DIRECTOR_URL={normalized.director_url}",
        (
            "PQNAS_CLUSTER_CREDENTIAL_FILE="
            f"{normalized.cluster_credential_file}"
        ),
        (
            "PQNAS_NODE_ENROLLMENT_ID="
            f"{normalized.enrollment_id}"
        ),
        (
            "PQNAS_NODE_ENROLLMENT_GENERATION="
            f"{normalized.enrollment_generation}"
        ),
        (
            "PQNAS_NODE_API_BASE_URL="
            f"{normalized.node_api_base_url}"
        ),
        (
            "PQNAS_NODE_BOOTSTRAP_TOKEN_FILE="
            f"{normalized.bootstrap_token_file}"
        ),
    ])

    return lines
