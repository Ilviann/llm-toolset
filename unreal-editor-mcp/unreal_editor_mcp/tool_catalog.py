"""Ordered, access-aware assembly of the Unreal Editor MCP tool catalog."""

from __future__ import annotations

from typing import Final

from .tool_catalog_families.assets import ASSET_TOOLS
from .tool_catalog_families.blueprints import BLUEPRINT_TOOLS
from .tool_catalog_families.core import CORE_TOOLS
from .tool_catalog_families.game_data import GAME_DATA_TOOLS
from .tool_catalog_families.gameplay_framework import GAMEPLAY_TOOLS
from .tool_catalog_families.levels import LEVEL_TOOLS
from .tool_catalog_families.lifecycle import EDITOR_LIFECYCLE_TOOL
from .tool_catalog_families.widgets import WIDGET_TOOLS


SUPPORTED_PROTOCOLS: Final = ("2024-11-05", "2025-03-26", "2025-06-18")
LATEST_PROTOCOL: Final = SUPPORTED_PROTOCOLS[-1]

TOOLS: Final = (
    *CORE_TOOLS,
    *ASSET_TOOLS,
    *LEVEL_TOOLS,
    *BLUEPRINT_TOOLS,
    *WIDGET_TOOLS,
    *GAMEPLAY_TOOLS,
    *GAME_DATA_TOOLS,
)

READONLY_TOOL_NAMES: Final = frozenset({
    "capabilities",
    "editor_state",
    "operation_status",
    "asset_inspect",
    "asset_references",
    "level_inspect",
    "level_open",
    "blueprint_action_catalog",
    "game_data_inspect",
})
WRITABLE_TOOL_NAMES: Final = frozenset({
    "operation_cancel",
    "asset_delete",
    "level_manage",
    "level_actor_edit",
    "level_save",
    "blueprint_graph_edit",
    "blueprint_block_replace",
    "blueprint_create",
    "blueprint_compile",
    "blueprint_save",
    "blueprint_component_edit",
    "blueprint_default_edit",
    "blueprint_member_edit",
    "widget_tree_edit",
    "gameplay_framework_edit",
    "game_data_edit",
})

_PUBLIC_NAMES = tuple(tool["name"] for tool in TOOLS)
if len(_PUBLIC_NAMES) != len(set(_PUBLIC_NAMES)):
    raise RuntimeError("Public tool names must be unique")
if READONLY_TOOL_NAMES & WRITABLE_TOOL_NAMES:
    raise RuntimeError("Public tools must have exactly one access classification")
if READONLY_TOOL_NAMES | WRITABLE_TOOL_NAMES != set(_PUBLIC_NAMES):
    raise RuntimeError("Every public tool must have an explicit access classification")

READONLY_TOOLS: Final = tuple(tool for tool in TOOLS if tool["name"] in READONLY_TOOL_NAMES)
TOOLS_WITH_LIFECYCLE: Final = (*TOOLS, EDITOR_LIFECYCLE_TOOL)
READONLY_TOOLS_WITH_LIFECYCLE: Final = (*READONLY_TOOLS, EDITOR_LIFECYCLE_TOOL)


def tools_for_configuration(
    *,
    writable: bool,
    lifecycle_enabled: bool,
) -> tuple[dict[str, object], ...]:
    """Return the deterministic public catalog for immutable startup access."""
    if type(writable) is not bool or type(lifecycle_enabled) is not bool:
        raise TypeError("Tool catalog configuration flags must be Boolean")
    tools = TOOLS if writable else READONLY_TOOLS
    return (*tools, EDITOR_LIFECYCLE_TOOL) if lifecycle_enabled else tools


TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
