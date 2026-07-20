"""Strict reader for the non-secret bridge discovery heartbeat."""

from __future__ import annotations

import json
import os
import stat
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Callable

from .errors import BridgeError, ErrorCode
from .project import ProjectLayout
from .platforms import DEFAULT_PLATFORM, PlatformAdapter


MAX_DISCOVERY_BYTES = 4096
MAX_DISCOVERY_AGE_MS = 10_000
TOKEN_HEX_LENGTH = 64
PROJECT_HASH_LENGTH = 40


@dataclass(frozen=True)
class DiscoveryRecord:
    project_hash: str
    process_id: int
    port: int
    bridge_version: str
    unreal_version: str
    updated_at_ms: int


def _read_regular_file(path: Path, maximum: int) -> bytes:
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(path, flags)
        try:
            metadata = os.fstat(descriptor)
            if not stat.S_ISREG(metadata.st_mode) or metadata.st_size > maximum:
                raise OSError("invalid generated-state file")
            data = os.read(descriptor, maximum + 1)
            if len(data) > maximum:
                raise OSError("generated-state file is too large")
            return data
        finally:
            os.close(descriptor)
    except OSError:
        raise BridgeError(
            "Unreal bridge generated state is unavailable; open the project with the plugin enabled",
            code=ErrorCode.EDITOR_UNAVAILABLE,
            retryable=True,
        ) from None


def read_token(layout: ProjectLayout) -> str:
    try:
        token = _read_regular_file(layout.token_file, 128).decode("ascii").strip()
    except UnicodeDecodeError:
        raise BridgeError(
            "Unreal bridge token is invalid; restart the editor plugin",
            code=ErrorCode.INVALID_CONFIGURATION,
        ) from None
    if len(token) != TOKEN_HEX_LENGTH or any(character not in "0123456789abcdef" for character in token):
        raise BridgeError(
            "Unreal bridge token is invalid; restart the editor plugin",
            code=ErrorCode.INVALID_CONFIGURATION,
        )
    return token


def read_discovery(
    layout: ProjectLayout,
    *,
    now_ms: Callable[[], int] = lambda: time.time_ns() // 1_000_000,
    maximum_age_ms: int = MAX_DISCOVERY_AGE_MS,
    platform: PlatformAdapter = DEFAULT_PLATFORM,
) -> DiscoveryRecord:
    raw = _read_regular_file(layout.discovery_file, MAX_DISCOVERY_BYTES)
    try:
        value = json.loads(raw)
    except (UnicodeDecodeError, json.JSONDecodeError):
        raise BridgeError(
            "Unreal bridge discovery record is invalid",
            code=ErrorCode.INVALID_CONFIGURATION,
        ) from None
    if not isinstance(value, dict) or set(value) != {
        "project_hash", "process_id", "port", "bridge_version", "unreal_version", "updated_at_ms"
    }:
        raise BridgeError("Unreal bridge discovery record is invalid", code=ErrorCode.INVALID_CONFIGURATION)
    record = _validated_record(value)
    age = now_ms() - record.updated_at_ms
    if age < -2_000 or age > maximum_age_ms:
        raise BridgeError(
            "Unreal bridge discovery record is stale",
            code=ErrorCode.EDITOR_UNAVAILABLE,
            details={"age_ms": age},
            retryable=True,
        )
    if not platform.process_is_alive(record.process_id):
        raise BridgeError(
            "Unreal bridge process is not running",
            code=ErrorCode.EDITOR_UNAVAILABLE,
            details={"process_id": record.process_id},
            retryable=True,
        )
    return record


def _validated_record(value: dict[str, Any]) -> DiscoveryRecord:
    project_hash = value["project_hash"]
    process_id = value["process_id"]
    port = value["port"]
    bridge_version = value["bridge_version"]
    unreal_version = value["unreal_version"]
    updated_at_ms = value["updated_at_ms"]
    valid = (
        isinstance(project_hash, str)
        and len(project_hash) == PROJECT_HASH_LENGTH
        and all(character in "0123456789abcdef" for character in project_hash)
        and type(process_id) is int and process_id > 0
        and type(port) is int and 1 <= port <= 65535
        and isinstance(bridge_version, str) and 1 <= len(bridge_version) <= 32
        and isinstance(unreal_version, str) and 1 <= len(unreal_version) <= 128
        and type(updated_at_ms) is int and updated_at_ms > 0
    )
    if not valid:
        raise BridgeError("Unreal bridge discovery record is invalid", code=ErrorCode.INVALID_CONFIGURATION)
    return DiscoveryRecord(project_hash, process_id, port, bridge_version, unreal_version, updated_at_ms)
