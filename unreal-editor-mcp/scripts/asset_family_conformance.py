"""Reusable asset-family conformance fixtures for repository verification.

The helpers in this module deliberately test published contracts without becoming
runtime input.  A new family opts into the common Python, packaging, and
cross-process gates by declaring one of the bounded fixtures below.
"""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Mapping, Protocol

from scripts.unreal_tooling.plugins import PluginIdentity
from unreal_editor_mcp.asset_family_catalog import (
    BRIDGE_HANDLER,
    COMPANION_API_VERSION,
    JSON_RESULT,
    MUTATION_ACCESS,
    READ_ACCESS,
    AssetFamilyPublication,
    StaticAssetFamilyCatalog,
)


_SNAPSHOT_LENGTH = 40


@dataclass(frozen=True)
class CatalogToolExpectation:
    """Expected access, dispatch, and result policy for one published tool."""

    name: str
    access: str
    result_handler: str = JSON_RESULT


@dataclass(frozen=True)
class CatalogFamilyFixture:
    """One exact built-in or companion Python publication contract."""

    family_id: str
    tools: tuple[CatalogToolExpectation, ...] = ()
    extension_id: str | None = None
    contributions: tuple[tuple[str, str, str], ...] = ()


@dataclass(frozen=True)
class PackageFamilyFixture:
    """One repository plugin that packages an asset-family implementation."""

    family_id: str
    plugin: PluginIdentity
    modules: tuple[str, ...]


@dataclass(frozen=True)
class InspectionVariant:
    """A selector or paging request that must retain the root snapshot."""

    arguments: Mapping[str, object]


@dataclass(frozen=True)
class CrossProcessFamilyFixture:
    """A deterministic inspection/read-back fixture for a live bridge."""

    family_id: str
    command: str
    arguments: Mapping[str, object]
    expected_fields: tuple[tuple[tuple[str, ...], object], ...]
    variants: tuple[InspectionVariant, ...] = ()
    ignored_determinism_fields: tuple[str, ...] = ("next_cursor",)


class BridgeLike(Protocol):
    def call(self, command: str, arguments: dict[str, object]) -> dict[str, object]: ...


def _catalog_entry(
    catalog: StaticAssetFamilyCatalog,
    family_id: str,
) -> AssetFamilyPublication:
    matches = [entry for entry in catalog.entries if entry.family_id == family_id]
    if len(matches) != 1:
        raise AssertionError(
            f"asset-family catalog must contain exactly one {family_id!r} entry"
        )
    return matches[0]


def _companion_capabilities(
    entry: AssetFamilyPublication,
    *,
    ready: bool = True,
    schema_revision: int | None = None,
) -> dict[str, object]:
    return {
        "companion_api_version": COMPANION_API_VERSION,
        "companions": [{
            "extension_id": entry.extension_id,
            "companion_api_version": COMPANION_API_VERSION,
            "schema_revision": (
                entry.schema_revision if schema_revision is None else schema_revision
            ),
            "ready": ready,
            "contributions": [
                {
                    "tool_family": contribution.tool_name,
                    "operation": contribution.operation,
                    "access": (
                        "read" if contribution.access == READ_ACCESS else "mutation"
                    ),
                }
                for contribution in entry.contributions
            ],
        }],
    }


def _published_operations(
    tools: tuple[dict[str, object], ...],
) -> set[tuple[str, str]]:
    operations: set[tuple[str, str]] = set()
    for tool in tools:
        schema = tool.get("inputSchema")
        branches = schema.get("oneOf") if isinstance(schema, dict) else None
        if not isinstance(branches, list):
            continue
        for branch in branches:
            properties = branch.get("properties") if isinstance(branch, dict) else None
            operation = properties.get("operation") if isinstance(properties, dict) else None
            value = operation.get("const") if isinstance(operation, dict) else None
            if isinstance(value, str):
                operations.add((str(tool.get("name")), value))
    return operations


def verify_catalog_family(
    catalog: StaticAssetFamilyCatalog,
    fixture: CatalogFamilyFixture,
) -> None:
    """Run deterministic publication, access, and unavailable-state gates."""

    entry = _catalog_entry(catalog, fixture.family_id)
    actual_tools = tuple(
        CatalogToolExpectation(item.name, item.access, item.result_handler)
        for item in entry.tools
    )
    if actual_tools != fixture.tools:
        raise AssertionError(
            f"{fixture.family_id} publication mismatch: {actual_tools!r}"
        )
    if entry.extension_id != fixture.extension_id:
        raise AssertionError(f"{fixture.family_id} extension identity mismatch")
    actual_contributions = tuple(item.key for item in entry.contributions)
    if actual_contributions != fixture.contributions:
        raise AssertionError(
            f"{fixture.family_id} contribution mismatch: {actual_contributions!r}"
        )

    before = deepcopy(tuple(publication.definition for publication in entry.tools))
    writable_first = catalog.compose(writable=True, lifecycle_enabled=True)
    writable_second = catalog.compose(writable=True, lifecycle_enabled=True)
    if writable_first.tools != writable_second.tools:
        raise AssertionError(f"{fixture.family_id} composition is not deterministic")
    if before != tuple(publication.definition for publication in entry.tools):
        raise AssertionError(f"{fixture.family_id} composition mutated static schemas")

    readonly = catalog.compose(writable=False, lifecycle_enabled=True)
    readonly_names = {str(tool["name"]) for tool in readonly.tools}
    writable_names = {str(tool["name"]) for tool in writable_first.tools}
    for expected in fixture.tools:
        if expected.name not in writable_names:
            raise AssertionError(f"{expected.name} is unavailable in writable composition")
        if (expected.name in readonly_names) != (expected.access == READ_ACCESS):
            raise AssertionError(f"{expected.name} readonly access mismatch")
        publication = writable_first.publications[expected.name]
        if publication.handler == BRIDGE_HANDLER and publication.native_command is None:
            raise AssertionError(f"{expected.name} has no native command")

        commands = {
            command
            for candidate in catalog.entries
            for item in candidate.tools
            for command in item.required_native_commands
        }
        commands.difference_update(publication.required_native_commands)
        unavailable = catalog.compose(
            writable=True,
            lifecycle_enabled=True,
            native_capabilities={"commands": sorted(commands)},
        )
        unavailable_names = {str(tool["name"]) for tool in unavailable.tools}
        if publication.required_native_commands and expected.name in unavailable_names:
            raise AssertionError(f"{expected.name} ignored an unavailable native command")

    if fixture.extension_id is None:
        return

    capabilities = _companion_capabilities(entry)
    writable_companion = catalog.compose(
        writable=True,
        lifecycle_enabled=False,
        native_capabilities=capabilities,
    )
    readonly_companion = catalog.compose(
        writable=False,
        lifecycle_enabled=False,
        native_capabilities=capabilities,
    )
    writable_operations = _published_operations(writable_companion.tools)
    readonly_operations = _published_operations(readonly_companion.tools)
    known_tool_names = {
        publication.name
        for candidate in catalog.entries
        for publication in candidate.tools
    }
    for tool_name, operation, access in fixture.contributions:
        expected_writable = tool_name in known_tool_names
        if ((tool_name, operation) in writable_operations) != expected_writable:
            raise AssertionError(f"companion operation availability mismatch: {operation}")
        expected_readonly = expected_writable and access == READ_ACCESS
        if ((tool_name, operation) in readonly_operations) != expected_readonly:
            raise AssertionError(f"companion readonly operation mismatch: {operation}")

    for rejected in (
        _companion_capabilities(entry, ready=False),
        _companion_capabilities(entry, schema_revision=(entry.schema_revision or 0) + 1),
    ):
        rejected_tools = catalog.compose(
            writable=True,
            lifecycle_enabled=False,
            native_capabilities=rejected,
        ).tools
        rejected_operations = _published_operations(rejected_tools)
        if any(
            (tool_name, operation) in rejected_operations
            for tool_name, operation, _ in fixture.contributions
        ):
            raise AssertionError(f"{fixture.family_id} did not fail closed")


def verify_source_package_fixture(fixture: PackageFamilyFixture) -> None:
    """Validate descriptor identity, modules, and repository dependency edges."""

    from scripts.packaging.service import read_plugin_descriptor

    descriptor = read_plugin_descriptor(fixture.plugin.descriptor)
    modules = descriptor.get("Modules")
    if not isinstance(modules, list):
        raise AssertionError(f"{fixture.family_id} descriptor has no module list")
    module_names = tuple(
        str(module.get("Name"))
        for module in modules
        if isinstance(module, dict)
    )
    if module_names != fixture.modules:
        raise AssertionError(
            f"{fixture.family_id} packaged modules mismatch: {module_names!r}"
        )
    plugins = descriptor.get("Plugins", [])
    declared_dependencies = {
        str(plugin.get("Name"))
        for plugin in plugins
        if isinstance(plugin, dict) and plugin.get("Enabled") is True
    }
    if declared_dependencies != set(fixture.plugin.dependencies):
        raise AssertionError(
            f"{fixture.family_id} plugin dependencies mismatch: {declared_dependencies!r}"
        )


def verify_packaged_family(fixture: PackageFamilyFixture, output: Path) -> None:
    """Apply normal package verification plus exact family-module presence."""

    from scripts.packaging.service import verify_package

    verify_package(output, fixture.plugin.descriptor)
    binary_names = {
        path.name.lower()
        for path in (output / "Binaries").rglob("*")
        if path.is_file()
    }
    for module in fixture.modules:
        if not any(module.lower() in name for name in binary_names):
            raise AssertionError(
                f"{fixture.family_id} package has no binary for module {module}"
            )


def _field(result: Mapping[str, object], path: tuple[str, ...]) -> object:
    value: object = result
    for segment in path:
        if not isinstance(value, Mapping) or segment not in value:
            raise AssertionError(f"missing conformance field: {'/'.join(path)}")
        value = value[segment]
    return value


def _snapshot(result: Mapping[str, object], family_id: str) -> str:
    value = result.get("snapshot_id")
    if not isinstance(value, str) or len(value) != _SNAPSHOT_LENGTH:
        raise AssertionError(f"{family_id} returned an invalid snapshot: {value!r}")
    if any(character not in "0123456789abcdef" for character in value):
        raise AssertionError(f"{family_id} snapshot is not lowercase hexadecimal")
    return value


def _without_fields(
    result: Mapping[str, object],
    ignored_fields: tuple[str, ...],
) -> dict[str, object]:
    return {key: value for key, value in result.items() if key not in ignored_fields}


def verify_cross_process_family(
    bridge: BridgeLike,
    fixture: CrossProcessFamilyFixture,
    *,
    preservation_probe: Callable[[], object] | None = None,
) -> dict[str, object]:
    """Verify identity, deterministic read-back, variants, and preservation."""

    before = preservation_probe() if preservation_probe is not None else None
    arguments = dict(fixture.arguments)
    first = bridge.call(fixture.command, arguments)
    second = bridge.call(fixture.command, arguments)
    snapshot = _snapshot(first, fixture.family_id)
    if _snapshot(second, fixture.family_id) != snapshot:
        raise AssertionError(f"{fixture.family_id} snapshot is not deterministic")
    if _without_fields(first, fixture.ignored_determinism_fields) != _without_fields(
        second, fixture.ignored_determinism_fields
    ):
        raise AssertionError(f"{fixture.family_id} encoding is not deterministic")
    for path, expected in fixture.expected_fields:
        if _field(first, path) != expected:
            raise AssertionError(
                f"{fixture.family_id} identity mismatch at {'/'.join(path)}"
            )
    for variant in fixture.variants:
        variant_result = bridge.call(
            fixture.command,
            {**arguments, **dict(variant.arguments)},
        )
        if _snapshot(variant_result, fixture.family_id) != snapshot:
            raise AssertionError(
                f"{fixture.family_id} selector or page changed the root snapshot"
            )
    if preservation_probe is not None and preservation_probe() != before:
        raise AssertionError(f"{fixture.family_id} inspection changed preserved state")
    return first


def verify_restart_read_back(
    bridge: BridgeLike,
    fixture: CrossProcessFamilyFixture,
    expected_snapshot: str,
) -> dict[str, object]:
    """Re-run a fixture after restart and require the persisted snapshot."""

    result = verify_cross_process_family(bridge, fixture)
    if result.get("snapshot_id") != expected_snapshot:
        raise AssertionError(f"{fixture.family_id} snapshot changed after restart")
    return result


def verify_recovered_mutation(
    family_id: str,
    status: Mapping[str, object],
    *,
    expected_fields: tuple[tuple[tuple[str, ...], object], ...] = (),
) -> Mapping[str, object]:
    """Validate one committed replay/lost-response ledger result."""

    result = status.get("result")
    if status.get("state") != "committed" or not isinstance(result, Mapping):
        raise AssertionError(f"{family_id} mutation did not reconcile: {status!r}")
    for path, expected in expected_fields:
        if _field(result, path) != expected:
            raise AssertionError(
                f"{family_id} recovered result mismatch at {'/'.join(path)}"
            )
    return result
