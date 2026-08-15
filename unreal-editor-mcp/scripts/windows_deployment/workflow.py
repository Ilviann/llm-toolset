"""Deployment planning, streamed packaging, and workflow coordination."""

from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path
from typing import Callable

try:
    from scripts import packaging
    from scripts.unreal_tooling.plugins import WORKSPACE_ROOT
except ModuleNotFoundError:
    import packaging  # type: ignore[no-redef]
    from unreal_tooling.plugins import WORKSPACE_ROOT  # type: ignore[no-redef]

from .discovery import DeploymentError, validate_supported_engine_root
from .models import (
    BASE_PLUGIN,
    COMMONUI_PLUGIN,
    ENHANCED_INPUT_PLUGIN,
    GAS_PLUGIN,
    INSTALL_IN_ENGINE_DISABLED,
    INSTALL_IN_ENGINE_ENABLED,
    INSTALL_IN_PROJECT,
    INSTALL_METHODS,
    DeploymentPlan,
    DeploymentRequest,
    DeploymentResult,
    PluginBuild,
    ProjectInfo,
)
from .transaction import (
    engine_plugin_destination,
    install_binary_plugins,
    plugin_destination,
    project_descriptor_update,
    write_project_descriptor,
)


def validate_install_method(install_method: str) -> str:
    if install_method not in INSTALL_METHODS:
        raise DeploymentError(f"unsupported install method: {install_method!r}")
    return install_method


def selected_plugins(
    *,
    include_gas: bool,
    include_commonui: bool,
    include_enhanced_input: bool = False,
) -> tuple[PluginBuild, ...]:
    if type(include_gas) is not bool:
        raise DeploymentError("include_gas must be Boolean")
    if type(include_commonui) is not bool:
        raise DeploymentError("include_commonui must be Boolean")
    if type(include_enhanced_input) is not bool:
        raise DeploymentError("include_enhanced_input must be Boolean")
    plugins = [BASE_PLUGIN]
    if include_gas:
        plugins.append(GAS_PLUGIN)
    if include_commonui:
        plugins.append(COMMONUI_PLUGIN)
    if include_enhanced_input:
        plugins.append(ENHANCED_INPUT_PLUGIN)
    return tuple(plugins)


def deployment_destinations(
    project: ProjectInfo,
    engine_root: Path,
    install_method: str,
    *,
    include_gas: bool,
    include_commonui: bool = False,
    include_enhanced_input: bool = False,
) -> tuple[Path, ...]:
    validate_install_method(install_method)
    plugins = selected_plugins(
        include_gas=include_gas,
        include_commonui=include_commonui,
        include_enhanced_input=include_enhanced_input,
    )
    if install_method == INSTALL_IN_PROJECT:
        return tuple(plugin_destination(project, plugin.name) for plugin in plugins)
    return tuple(engine_plugin_destination(engine_root, plugin.name) for plugin in plugins)


def build_command(engine_root: Path, output: Path, plugin: PluginBuild = BASE_PLUGIN) -> list[str]:
    try:
        run_uat = validate_supported_engine_root(engine_root)
        output = packaging.validate_output(output, engine_root, plugin.descriptor)
        return packaging.build_command(
            run_uat,
            output,
            "Win64",
            strict_includes=False,
            unversioned=False,
            plugin_descriptor=plugin.descriptor,
            dependency_plugins=plugin.dependency_plugins,
        )
    except packaging.PackagingError as error:
        raise DeploymentError(str(error)) from error


def run_packaging(engine_root: Path, output: Path, log: Callable[[str], None], plugin: PluginBuild = BASE_PLUGIN) -> None:
    command = build_command(engine_root, output, plugin)
    log(f"Building installed Win64 {plugin.name} plugin with {engine_root}")
    try:
        process = subprocess.Popen(
            command,
            cwd=WORKSPACE_ROOT,
            env=os.environ.copy(),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0),
        )
    except OSError as error:
        raise DeploymentError(f"could not start Unreal AutomationTool: {error}") from error
    assert process.stdout is not None
    for line in process.stdout:
        log(line.rstrip())
    return_code = process.wait()
    if return_code != 0:
        raise DeploymentError(f"Unreal AutomationTool failed with exit code {return_code}")
    try:
        packaging.restore_source_descriptor_contract(output, plugin.descriptor)
        packaging.verify_package(output, plugin.descriptor)
    except packaging.PackagingError as error:
        raise DeploymentError(str(error)) from error


def plan_deployment(request: DeploymentRequest) -> DeploymentPlan:
    if type(request.replace_existing) is not bool:
        raise DeploymentError("replace_existing must be Boolean")
    validate_install_method(request.install_method)
    plugins = selected_plugins(
        include_gas=request.include_gas,
        include_commonui=request.include_commonui,
        include_enhanced_input=request.include_enhanced_input,
    )
    destinations = deployment_destinations(
        request.project,
        request.engine_root,
        request.install_method,
        include_gas=request.include_gas,
        include_commonui=request.include_commonui,
        include_enhanced_input=request.include_enhanced_input,
    )
    existing = tuple(destination for destination in destinations if destination.exists())
    if existing and not request.replace_existing:
        raise DeploymentError("selected plugin installation already exists: " + ", ".join(str(path) for path in existing))
    enabled = True if request.install_method == INSTALL_IN_ENGINE_ENABLED else False if request.install_method == INSTALL_IN_ENGINE_DISABLED else None
    descriptor = project_descriptor_update(request.project, [plugin.name for plugin in plugins]) if request.install_method == INSTALL_IN_PROJECT else None
    return DeploymentPlan(request, plugins, destinations, existing, enabled, descriptor)


def execute_deployment(plan: DeploymentPlan, log: Callable[[str], None]) -> DeploymentResult:
    request = plan.request
    with tempfile.TemporaryDirectory(prefix="unreal-mcp-package-") as temporary:
        package_roots: list[Path] = []
        for plugin in plan.plugins:
            package_root = Path(temporary) / plugin.name
            run_packaging(request.engine_root, package_root, log, plugin)
            package_roots.append(package_root)
        log("Removing implementation source and debug artifacts except matching Win64 PDBs" if request.include_pdb else "Removing implementation source and debug-symbol artifacts")
        current = tuple(destination for destination in plan.destinations if destination.exists())
        if current != plan.existing_destinations:
            raise DeploymentError("selected plugin installation state changed while packages were building")
        installed = install_binary_plugins(
            tuple(zip(plan.plugins, package_roots, plan.destinations)),
            replace_existing=request.replace_existing,
            include_pdb=request.include_pdb,
            enabled_by_default=plan.enabled_by_default,
            after_install=(lambda: write_project_descriptor(request.project, plan.project_descriptor[1], expected_original=plan.project_descriptor[0])) if plan.project_descriptor is not None else None,
        )
    for destination in installed:
        log(f"Installed binary plugin at {destination}")
    return DeploymentResult(installed)


def deploy(
    project: ProjectInfo,
    engine_root: Path,
    *,
    replace_existing: bool,
    include_pdb: bool = False,
    include_gas: bool = False,
    include_commonui: bool = False,
    include_enhanced_input: bool = False,
    install_method: str = INSTALL_IN_PROJECT,
    log: Callable[[str], None],
) -> tuple[Path, ...]:
    request = DeploymentRequest(
        project=project,
        engine_root=engine_root,
        replace_existing=replace_existing,
        include_pdb=include_pdb,
        include_gas=include_gas,
        include_commonui=include_commonui,
        install_method=install_method,
        include_enhanced_input=include_enhanced_input,
    )
    return execute_deployment(plan_deployment(request), log).destinations
