"""Typed requests and results for Unreal plugin packaging."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
from typing import Mapping


@dataclass(frozen=True)
class PackageRequest:
    engine_root: Path
    output: Path
    plugin_descriptor: Path
    dependency_plugins: tuple[Path, ...] = ()
    target_platforms: str | None = None
    strict_includes: bool = False
    unversioned: bool = False
    developer_dir: Path | None = None
    host_system: str = "Windows"


@dataclass(frozen=True)
class PreparedPackage:
    request: PackageRequest
    run_uat: Path
    output: Path
    command: tuple[str, ...]
    environment: Mapping[str, str]


@dataclass(frozen=True)
class PackageResult:
    output: Path
    command: tuple[str, ...]
    return_code: int
