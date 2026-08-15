"""Static Python publication catalog for Unreal asset-operation families."""

from __future__ import annotations

from copy import deepcopy
from dataclasses import dataclass
from types import MappingProxyType
from typing import Final, Iterable, Mapping

from .tool_catalog_families.assets import ASSET_TOOLS
from .tool_catalog_families.blueprints import BLUEPRINT_TOOLS
from .tool_catalog_families.core import CORE_TOOLS
from .tool_catalog_families.game_data import GAME_DATA_TOOLS
from .tool_catalog_families.gameplay_framework import GAMEPLAY_TOOLS
from .tool_catalog_families.levels import LEVEL_TOOLS
from .tool_catalog_families.lifecycle import EDITOR_LIFECYCLE_TOOL
from .tool_catalog_families.widgets import WIDGET_TOOLS


READ_ACCESS: Final = "read"
MUTATION_ACCESS: Final = "mutation"
JSON_RESULT: Final = "json"
SAFE_YAML_RESULT: Final = "safe_yaml"
BRIDGE_HANDLER: Final = "bridge"
CAPABILITIES_HANDLER: Final = "capabilities"
LIFECYCLE_HANDLER: Final = "lifecycle"

COMPANION_API_VERSION: Final = 2
EXTENSION_SCHEMA_REVISION: Final = 2

_NATIVE_ASSET_FAMILY_FIELDS: Final = frozenset({
    "family_id", "native_class", "class_policy", "priority", "operations",
    "creation_persistence", "editing_persistence", "limits", "selector_routes",
    "stable_nested_identity_kinds",
})
_PERSISTENCE_POLICIES: Final = {
    "none", "package_save", "blueprint_compile_and_save",
}

_PATH: Final = {
    "type": "string", "minLength": 3, "maxLength": 512,
    "pattern": r"^(?!.*\.\.)(?!.*\\)/[^\s]+$",
}
_OPERATION_ID: Final = {"type": "string", "pattern": r"^[0-9a-f]{32}$"}
_SNAPSHOT: Final = {"type": "string", "pattern": r"^[0-9a-f]{40}$"}


@dataclass(frozen=True)
class ToolPublication:
    """One exact public tool binding owned by a publication family."""

    definition: dict[str, object]
    access: str
    native_command: str | None
    handler: str = BRIDGE_HANDLER
    result_handler: str = JSON_RESULT
    required_native_commands: tuple[str, ...] = ()

    @property
    def name(self) -> str:
        return self.definition["name"]  # type: ignore[return-value]


@dataclass(frozen=True)
class CompanionContribution:
    """One shipped schema branch matched to an exact native contribution."""

    tool_name: str
    operation: str
    access: str
    input_schema: dict[str, object]

    @property
    def key(self) -> tuple[str, str, str]:
        return self.tool_name, self.operation, self.access


@dataclass(frozen=True)
class IntegratedSections:
    """Static semantic sections enabled by one exact native contribution."""

    tool_name: str
    operation: str
    access: str
    sections: tuple[str, ...]

    @property
    def key(self) -> tuple[str, str, str]:
        return self.tool_name, self.operation, self.access


@dataclass(frozen=True)
class AssetFamilyPublication:
    """One approved built-in or companion Python publication entry."""

    family_id: str
    tools: tuple[ToolPublication, ...] = ()
    extension_id: str | None = None
    schema_revision: int | None = None
    contributions: tuple[CompanionContribution, ...] = ()
    integrated_sections: tuple[IntegratedSections, ...] = ()
    native_family_ids: tuple[str, ...] = ()


@dataclass(frozen=True)
class CatalogComposition:
    """Deterministic configuration-specific tools plus their dispatch metadata."""

    tools: tuple[dict[str, object], ...]
    publications: Mapping[str, ToolPublication]


class StaticAssetFamilyCatalog:
    """Validated immutable family entries and deterministic composition."""

    def __init__(self, entries: Iterable[AssetFamilyPublication]) -> None:
        self.entries = tuple(entries)
        self._validate()
        self.publications = MappingProxyType({
            publication.name: publication
            for entry in self.entries
            for publication in entry.tools
        })
        self.companions = MappingProxyType({
            entry.extension_id: entry
            for entry in self.entries
            if entry.extension_id is not None
        })

    def _validate(self) -> None:
        family_ids: set[str] = set()
        tool_names: set[str] = set()
        native_mappings: dict[str, str] = {}
        extension_ids: set[str] = set()
        contribution_keys: set[tuple[str, str, str, str]] = set()
        for entry in self.entries:
            if not entry.family_id or entry.family_id in family_ids:
                raise RuntimeError(f"Duplicate or empty Python asset family: {entry.family_id!r}")
            family_ids.add(entry.family_id)
            if entry.extension_id is None:
                if (entry.schema_revision is not None or entry.contributions
                        or entry.integrated_sections or entry.native_family_ids):
                    raise RuntimeError(f"Built-in family {entry.family_id!r} has companion metadata")
            else:
                if entry.extension_id in extension_ids:
                    raise RuntimeError(f"Duplicate Python companion family: {entry.extension_id!r}")
                if type(entry.schema_revision) is not int or entry.schema_revision <= 0:
                    raise RuntimeError(f"Companion family {entry.family_id!r} has invalid schema revision")
                if (
                    len(entry.native_family_ids) != len(set(entry.native_family_ids))
                    or any(not family_id for family_id in entry.native_family_ids)
                ):
                    raise RuntimeError(
                        f"Companion family {entry.family_id!r} has invalid native family identities"
                    )
                extension_ids.add(entry.extension_id)
            for publication in entry.tools:
                definition = publication.definition
                if set(definition) != {"name", "description", "inputSchema"}:
                    raise RuntimeError(f"Invalid public tool definition in {entry.family_id!r}")
                name = definition.get("name")
                if not isinstance(name, str) or not name or name in tool_names:
                    raise RuntimeError(f"Duplicate or invalid public tool: {name!r}")
                if publication.access not in {READ_ACCESS, MUTATION_ACCESS}:
                    raise RuntimeError(f"Invalid access for public tool {name!r}")
                if publication.handler not in {
                    BRIDGE_HANDLER, CAPABILITIES_HANDLER, LIFECYCLE_HANDLER,
                }:
                    raise RuntimeError(f"Invalid handler for public tool {name!r}")
                if publication.result_handler not in {JSON_RESULT, SAFE_YAML_RESULT}:
                    raise RuntimeError(f"Invalid result handler for public tool {name!r}")
                if publication.handler == BRIDGE_HANDLER and not publication.native_command:
                    raise RuntimeError(f"Bridge tool {name!r} has no native command mapping")
                if publication.native_command:
                    mapped_name = native_mappings.get(publication.native_command)
                    if mapped_name is not None and mapped_name != name:
                        raise RuntimeError(
                            f"Conflicting native command mapping {publication.native_command!r}"
                        )
                    native_mappings[publication.native_command] = name
                if any(not command for command in publication.required_native_commands):
                    raise RuntimeError(f"Tool {name!r} has an invalid native requirement")
                tool_names.add(name)
            for contribution in entry.contributions:
                key = (entry.extension_id or "", *contribution.key)
                if key in contribution_keys or contribution.access not in {
                    READ_ACCESS, MUTATION_ACCESS,
                }:
                    raise RuntimeError(f"Duplicate or invalid companion contribution: {key!r}")
                contribution_keys.add(key)
            for sections in entry.integrated_sections:
                key = (entry.extension_id or "", *sections.key)
                if key in contribution_keys or sections.access not in {
                    READ_ACCESS, MUTATION_ACCESS,
                } or not sections.sections or len(sections.sections) != len(set(sections.sections)):
                    raise RuntimeError(f"Duplicate or invalid integrated contribution: {key!r}")
                contribution_keys.add(key)

    def compose(
        self,
        *,
        writable: bool,
        lifecycle_enabled: bool,
        native_capabilities: object = None,
    ) -> CatalogComposition:
        if type(writable) is not bool or type(lifecycle_enabled) is not bool:
            raise TypeError("Tool catalog configuration flags must be Boolean")
        native_commands = _native_command_set(native_capabilities)
        publications: dict[str, ToolPublication] = {}
        tools: list[dict[str, object]] = []
        for entry in self.entries:
            for publication in entry.tools:
                if publication.handler == LIFECYCLE_HANDLER and not lifecycle_enabled:
                    continue
                if publication.access == MUTATION_ACCESS and not writable:
                    continue
                if (
                    native_commands is not None
                    and not set(publication.required_native_commands).issubset(native_commands)
                ):
                    continue
                publications[publication.name] = publication
                tools.append(deepcopy(publication.definition))
        _compose_companion_branches(
            self.companions,
            tools,
            native_capabilities,
            writable=writable,
        )
        return CatalogComposition(tuple(tools), MappingProxyType(publications))


def _native_command_set(native_capabilities: object) -> set[str] | None:
    if not isinstance(native_capabilities, dict) or "commands" not in native_capabilities:
        return None
    commands = native_capabilities.get("commands")
    if not isinstance(commands, list) or len(commands) > 128:
        return set()
    if any(not isinstance(command, str) or not command for command in commands):
        return set()
    return set(commands)


def _read_shape(operation: str) -> dict[str, object]:
    return {
        "type": "object",
        "properties": {
            "extension_id": {"const": "unreal-mcp-test"},
            "extension_schema_revision": {"const": EXTENSION_SCHEMA_REVISION},
            "operation": {"const": operation},
            "asset_path": _PATH,
        },
        "required": ["extension_id", "extension_schema_revision", "operation", "asset_path"],
        "additionalProperties": False,
    }


def _mutation_shape(operation: str) -> dict[str, object]:
    return {
        "type": "object",
        "properties": {
            "extension_id": {"const": "unreal-mcp-test"},
            "extension_schema_revision": {"const": EXTENSION_SCHEMA_REVISION},
            "operation": {"const": operation},
            "operation_id": _OPERATION_ID,
            "asset_path": _PATH,
            "expected_snapshot": _SNAPSHOT,
            "value": {"type": "integer", "minimum": -1000000, "maximum": 1000000},
        },
        "required": [
            "extension_id", "extension_schema_revision", "operation", "operation_id",
            "asset_path", "expected_snapshot", "value",
        ],
        "additionalProperties": False,
    }


def _publications(
    tools: tuple[dict[str, object], ...],
    readonly: Iterable[str],
    *,
    local_handlers: Mapping[str, str] | None = None,
    result_handlers: Mapping[str, str] | None = None,
) -> tuple[ToolPublication, ...]:
    readonly_names = frozenset(readonly)
    local_handlers = local_handlers or {}
    result_handlers = result_handlers or {}
    return tuple(
        ToolPublication(
            definition=tool,
            access=READ_ACCESS if tool["name"] in readonly_names else MUTATION_ACCESS,
            native_command=(
                tool["name"] if local_handlers.get(tool["name"], BRIDGE_HANDLER) != LIFECYCLE_HANDLER
                else None
            ),
            handler=local_handlers.get(tool["name"], BRIDGE_HANDLER),
            result_handler=result_handlers.get(tool["name"], JSON_RESULT),
            required_native_commands=(
                (tool["name"],)
                if local_handlers.get(tool["name"], BRIDGE_HANDLER) != LIFECYCLE_HANDLER
                else ()
            ),
        )
        for tool in tools
    )


CATALOG_ENTRIES: Final = (
    AssetFamilyPublication(
        "core",
        _publications(
            CORE_TOOLS,
            ("capabilities", "editor_state", "operation_status"),
            local_handlers={"capabilities": CAPABILITIES_HANDLER},
        ),
    ),
    AssetFamilyPublication(
        "assets",
        _publications(
            ASSET_TOOLS,
            ("asset_inspect", "asset_references"),
            result_handlers={"asset_inspect": SAFE_YAML_RESULT},
        ),
    ),
    AssetFamilyPublication(
        "levels",
        _publications(LEVEL_TOOLS, ("level_inspect", "level_open")),
    ),
    AssetFamilyPublication(
        "blueprints",
        _publications(BLUEPRINT_TOOLS, ("blueprint_action_catalog",)),
    ),
    AssetFamilyPublication("widgets", _publications(WIDGET_TOOLS, ())),
    AssetFamilyPublication("gameplay-framework", _publications(GAMEPLAY_TOOLS, ())),
    AssetFamilyPublication(
        "game-data",
        _publications(GAME_DATA_TOOLS, ("game_data_inspect",)),
    ),
    AssetFamilyPublication(
        "editor-lifecycle",
        _publications(
            (EDITOR_LIFECYCLE_TOOL,),
            ("editor_lifecycle",),
            local_handlers={"editor_lifecycle": LIFECYCLE_HANDLER},
        ),
    ),
    AssetFamilyPublication(
        "test-companion",
        extension_id="unreal-mcp-test",
        schema_revision=EXTENSION_SCHEMA_REVISION,
        contributions=tuple(
            CompanionContribution(tool, operation, access, schema)
            for tool, operation, access, schema in (
                ("blueprint_inspect", "inspect_test_asset", READ_ACCESS, _read_shape("inspect_test_asset")),
                ("blueprint_default_edit", "set_test_asset_value", MUTATION_ACCESS, _mutation_shape("set_test_asset_value")),
                ("blueprint_inspect", "inspect_test_component", READ_ACCESS, _read_shape("inspect_test_component")),
                ("blueprint_component_edit", "set_test_component_value", MUTATION_ACCESS, _mutation_shape("set_test_component_value")),
                ("blueprint_inspect", "inspect_test_contribution", READ_ACCESS, _read_shape("inspect_test_contribution")),
                ("blueprint_default_edit", "set_test_contribution_value", MUTATION_ACCESS, _mutation_shape("set_test_contribution_value")),
            )
        ),
    ),
    AssetFamilyPublication(
        "gas-companion",
        extension_id="unreal-mcp-gas",
        schema_revision=EXTENSION_SCHEMA_REVISION,
        native_family_ids=(
            "attribute_set",
            "gameplay_ability",
            "gameplay_cue_notify_actor",
            "gameplay_cue_notify_static",
            "gameplay_effect",
            "gameplay_effect_execution_calculation",
            "gameplay_mod_magnitude_calculation",
        ),
    ),
    AssetFamilyPublication(
        "commonui-companion",
        extension_id="unreal-mcp-commonui",
        schema_revision=EXTENSION_SCHEMA_REVISION,
        native_family_ids=("commonui_widget",),
    ),
)

ASSET_FAMILY_CATALOG: Final = StaticAssetFamilyCatalog(CATALOG_ENTRIES)


def _valid_native_asset_families(value: object) -> bool:
    def stable_id(item: object) -> bool:
        return (
            isinstance(item, str) and 0 < len(item) <= 64
            and all(character in "abcdefghijklmnopqrstuvwxyz0123456789-_"
                    for character in item)
        )

    def unique_stable_ids(items: object, limit: int) -> bool:
        return (
            isinstance(items, list) and len(items) <= limit
            and all(stable_id(item) for item in items)
            and len(set(items)) == len(items)
        )

    if not isinstance(value, list) or len(value) > 16:
        return False
    for family in value:
        if not isinstance(family, dict) or set(family) != _NATIVE_ASSET_FAMILY_FIELDS:
            return False
        operations = family.get("operations")
        limits = family.get("limits")
        selectors = family.get("selector_routes")
        identities = family.get("stable_nested_identity_kinds")
        if (
            not stable_id(family.get("family_id"))
            or not isinstance(family.get("native_class"), str)
            or not 0 < len(family["native_class"]) <= 512
            or not family["native_class"].startswith("/")
            or family.get("class_policy") not in {"exact", "exact_and_derived"}
            or type(family.get("priority")) is not int
            or abs(family["priority"]) > 1000
            or not isinstance(operations, dict)
            or set(operations) != {"inspect", "create", "edit"}
            or any(type(operations[name]) is not bool for name in operations)
            or family.get("creation_persistence") not in _PERSISTENCE_POLICIES
            or family.get("editing_persistence") not in _PERSISTENCE_POLICIES
            or not isinstance(limits, dict) or len(limits) > 32
            or any(not stable_id(name) or type(limit) is not int or limit < 1
                   for name, limit in limits.items())
            or not unique_stable_ids(selectors, 64)
            or not unique_stable_ids(identities, 32)
        ):
            return False
    return True


def _matches_known_asset_families(
    known: AssetFamilyPublication,
    value: object,
) -> bool:
    if not _valid_native_asset_families(value) or not isinstance(value, list):
        return False
    if not known.native_family_ids:
        return True
    by_id = {
        family.get("family_id"): family
        for family in value if isinstance(family, dict)
    }
    if set(by_id) != set(known.native_family_ids):
        return False
    return all(
        family.get("operations") == {"inspect": True, "create": False, "edit": False}
        for family in by_id.values()
    )


def _compose_companion_branches(
    companions_catalog: Mapping[str, AssetFamilyPublication],
    tools: list[dict[str, object]],
    native_capabilities: object,
    *,
    writable: bool,
) -> None:
    if not isinstance(native_capabilities, dict):
        return
    if native_capabilities.get("companion_api_version") != COMPANION_API_VERSION:
        return
    companions = native_capabilities.get("companions")
    if not isinstance(companions, list):
        return
    accepted: list[CompanionContribution] = []
    integrated: dict[str, set[str]] = {}
    for companion in companions[:64]:
        if not isinstance(companion, dict) or companion.get("ready") is not True:
            continue
        extension_id = companion.get("extension_id")
        known = companions_catalog.get(extension_id) if isinstance(extension_id, str) else None
        if (
            known is None
            or companion.get("companion_api_version") != COMPANION_API_VERSION
            or companion.get("schema_revision") != known.schema_revision
            or not _matches_known_asset_families(
                known, companion.get("asset_families")
            )
        ):
            continue
        native_contributions = companion.get("contributions")
        if not isinstance(native_contributions, list) or len(native_contributions) > 32:
            continue
        native_keys = {
            (item.get("tool_family"), item.get("operation"), item.get("access"))
            for item in native_contributions if isinstance(item, dict)
        }
        for contribution in known.contributions:
            native_key = (
                contribution.tool_name,
                contribution.operation,
                "read" if contribution.access == READ_ACCESS else "mutation",
            )
            if native_key in native_keys and (
                contribution.access == READ_ACCESS or writable
            ):
                accepted.append(contribution)
        for sections in known.integrated_sections:
            native_key = (
                sections.tool_name,
                sections.operation,
                "read" if sections.access == READ_ACCESS else "mutation",
            )
            if native_key in native_keys and (sections.access == READ_ACCESS or writable):
                integrated.setdefault(sections.tool_name, set()).update(sections.sections)

    by_name = {tool["name"]: tool for tool in tools}
    for contribution in sorted(
        accepted,
        key=lambda item: (item.tool_name, item.operation, item.access),
    ):
        tool = by_name.get(contribution.tool_name)
        if tool is None:
            continue
        input_schema = tool["inputSchema"]
        if not isinstance(input_schema, dict):
            continue
        if isinstance(input_schema.get("oneOf"), list):
            input_schema["oneOf"].append(deepcopy(contribution.input_schema))
        else:
            tool["inputSchema"] = {
                "oneOf": [deepcopy(input_schema), deepcopy(contribution.input_schema)]
            }
    for tool_name, sections in integrated.items():
        tool = by_name.get(tool_name)
        if tool is None or not isinstance(tool.get("inputSchema"), dict):
            continue
        branches = tool["inputSchema"].get("oneOf")
        if not isinstance(branches, list):
            continue
        for branch in branches:
            if not isinstance(branch, dict):
                continue
            properties = branch.get("properties")
            if not isinstance(properties, dict):
                continue
            mode = properties.get("mode")
            section_schema = properties.get("sections")
            if not isinstance(mode, dict) or mode.get("const") != "inspect":
                continue
            if not isinstance(section_schema, dict):
                continue
            items = section_schema.get("items")
            values = items.get("enum") if isinstance(items, dict) else None
            if not isinstance(values, list):
                continue
            for section in sorted(sections):
                if section not in values:
                    values.append(section)
            section_schema["maxItems"] = len(values)


def compose_extension_tools(
    base_tools: tuple[dict[str, object], ...],
    native_capabilities: object,
    *,
    writable: bool,
) -> tuple[dict[str, object], ...]:
    """Compatibility facade for exact companion schema intersection."""
    tools = deepcopy(base_tools)
    _compose_companion_branches(
        ASSET_FAMILY_CATALOG.companions,
        tools,
        native_capabilities,
        writable=writable,
    )
    return tuple(tools)


def compose_companion_capabilities(native_capabilities: dict[str, object]) -> None:
    """Annotate bounded native records with effective Python-catalog readiness."""
    host_api_match = (
        native_capabilities.get("companion_api_version") == COMPANION_API_VERSION
    )
    companions = native_capabilities.get("companions")
    if not isinstance(companions, list):
        return
    for companion in companions[:64]:
        if not isinstance(companion, dict):
            continue
        extension_id = companion.get("extension_id")
        known = (
            ASSET_FAMILY_CATALOG.companions.get(extension_id)
            if isinstance(extension_id, str) else None
        )
        python_known = known is not None
        schema_match = (
            python_known
            and host_api_match
            and companion.get("schema_revision") == known.schema_revision
            and companion.get("companion_api_version") == COMPANION_API_VERSION
            and _matches_known_asset_families(
                known, companion.get("asset_families")
            )
        )
        native_ready = companion.get("ready") is True
        companion["python_known"] = python_known
        companion["effective_ready"] = native_ready and schema_match
        if native_ready and not python_known:
            companion["effective_unavailable_reason"] = "python_catalog_unknown"
        elif native_ready and not schema_match:
            companion["effective_unavailable_reason"] = "python_schema_mismatch"
        else:
            companion["effective_unavailable_reason"] = companion.get("unavailable_reason", "")
