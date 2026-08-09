"""Typed inputs, plans, and results for Windows plugin deployment."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path

try:
    from scripts.unreal_tooling.plugins import BASE_PLUGIN as BASE_IDENTITY
    from scripts.unreal_tooling.plugins import COMMONUI_PLUGIN as COMMONUI_IDENTITY
    from scripts.unreal_tooling.plugins import GAS_PLUGIN as GAS_IDENTITY
except ModuleNotFoundError:
    from unreal_tooling.plugins import BASE_PLUGIN as BASE_IDENTITY  # type: ignore[no-redef]
    from unreal_tooling.plugins import COMMONUI_PLUGIN as COMMONUI_IDENTITY  # type: ignore[no-redef]
    from unreal_tooling.plugins import GAS_PLUGIN as GAS_IDENTITY  # type: ignore[no-redef]


PLUGIN_NAME = BASE_IDENTITY.name
GAS_PLUGIN_NAME = GAS_IDENTITY.name
COMMONUI_PLUGIN_NAME = COMMONUI_IDENTITY.name
INSTALL_IN_PROJECT = "project"
INSTALL_IN_ENGINE_ENABLED = "engine_enabled"
INSTALL_IN_ENGINE_DISABLED = "engine_disabled"
INSTALL_METHODS = frozenset(
    {INSTALL_IN_PROJECT, INSTALL_IN_ENGINE_ENABLED, INSTALL_IN_ENGINE_DISABLED}
)
MAX_DEPLOYMENT_PLUGINS = 3


@dataclass(frozen=True)
class ProjectInfo:
    folder: Path
    descriptor: Path
    engine_association: str


@dataclass(frozen=True)
class PluginBuild:
    name: str
    descriptor: Path
    dependency_plugins: tuple[Path, ...] = ()


BASE_PLUGIN = PluginBuild(PLUGIN_NAME, BASE_IDENTITY.descriptor)
GAS_PLUGIN = PluginBuild(GAS_PLUGIN_NAME, GAS_IDENTITY.descriptor, (BASE_IDENTITY.descriptor,))
COMMONUI_PLUGIN = PluginBuild(
    COMMONUI_PLUGIN_NAME,
    COMMONUI_IDENTITY.descriptor,
    (BASE_IDENTITY.descriptor,),
)


@dataclass(frozen=True)
class DeploymentRequest:
    project: ProjectInfo
    engine_root: Path
    replace_existing: bool
    include_pdb: bool = False
    include_gas: bool = False
    include_commonui: bool = False
    install_method: str = INSTALL_IN_PROJECT


@dataclass(frozen=True)
class DeploymentPlan:
    request: DeploymentRequest
    plugins: tuple[PluginBuild, ...]
    destinations: tuple[Path, ...]
    existing_destinations: tuple[Path, ...]
    enabled_by_default: bool | None
    project_descriptor: tuple[bytes, bytes] | None


@dataclass(frozen=True)
class DeploymentResult:
    destinations: tuple[Path, ...]
