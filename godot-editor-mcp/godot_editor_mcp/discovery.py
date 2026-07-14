"""Project-scoped Godot bridge discovery records."""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path
import time
from typing import Any

from .errors import ProjectMismatchError


DISCOVERY_FILE = "godot_mcp_bridge.json"
DISCOVERY_PROTOCOL_VERSION = "1"
MAX_DISCOVERY_BYTES = 4096
DEFAULT_MAX_AGE_SECONDS = 5.0


def normalized_project_path(project: Path) -> str:
    path = str(project.resolve(strict=True)).replace("\\", "/").rstrip("/")
    return path.casefold() if os.name == "nt" else path


def project_path_hash(project: Path) -> str:
    return hashlib.sha256(normalized_project_path(project).encode("utf-8")).hexdigest()


@dataclass(frozen=True)
class DiscoveryRecord:
    process_id: int
    project_hash: str
    port: int
    bridge_version: str
    protocol_version: str
    heartbeat_unix_ms: int

    @classmethod
    def from_mapping(cls, value: Any) -> "DiscoveryRecord":
        if not isinstance(value, dict):
            raise ValueError("Discovery record must be an object")
        record = cls(
            process_id=value.get("process_id"),
            project_hash=value.get("project_hash"),
            port=value.get("port"),
            bridge_version=value.get("bridge_version"),
            protocol_version=value.get("protocol_version"),
            heartbeat_unix_ms=value.get("heartbeat_unix_ms"),
        )
        if type(record.process_id) is not int or record.process_id <= 0:
            raise ValueError("Invalid discovery process ID")
        if (
            not isinstance(record.project_hash, str)
            or len(record.project_hash) != 64
            or any(char not in "0123456789abcdef" for char in record.project_hash)
        ):
            raise ValueError("Invalid discovery project hash")
        if type(record.port) is not int or not 1 <= record.port <= 65535:
            raise ValueError("Invalid discovery port")
        if not isinstance(record.bridge_version, str) or not record.bridge_version:
            raise ValueError("Invalid discovery bridge version")
        if record.protocol_version != DISCOVERY_PROTOCOL_VERSION:
            raise ValueError("Unsupported discovery protocol version")
        if type(record.heartbeat_unix_ms) is not int or record.heartbeat_unix_ms <= 0:
            raise ValueError("Invalid discovery heartbeat")
        return record

    def is_live(
        self,
        *,
        now: float | None = None,
        max_age: float = DEFAULT_MAX_AGE_SECONDS,
    ) -> bool:
        current = time.time() if now is None else now
        age = current - (self.heartbeat_unix_ms / 1000.0)
        return -1.0 <= age <= max_age


def read_discovery_record(project: Path) -> DiscoveryRecord | None:
    path = project / ".godot" / DISCOVERY_FILE
    try:
        with path.open("rb") as stream:
            raw = stream.read(MAX_DISCOVERY_BYTES + 1)
    except FileNotFoundError:
        return None
    except OSError:
        return None
    if len(raw) > MAX_DISCOVERY_BYTES:
        return None
    try:
        record = DiscoveryRecord.from_mapping(json.loads(raw))
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError, TypeError):
        return None
    expected = project_path_hash(project)
    if record.project_hash != expected:
        raise ProjectMismatchError(
            "Godot bridge discovery record belongs to another project",
            details={
                "expected_project_hash": expected,
                "record_project_hash": record.project_hash,
            },
        )
    return record


def discovered_port(project: Path, fallback: int) -> int:
    record = read_discovery_record(project)
    if record is not None and record.is_live():
        return record.port
    return fallback


__all__ = [
    "DEFAULT_MAX_AGE_SECONDS",
    "DISCOVERY_FILE",
    "DISCOVERY_PROTOCOL_VERSION",
    "DiscoveryRecord",
    "discovered_port",
    "normalized_project_path",
    "project_path_hash",
    "read_discovery_record",
]
