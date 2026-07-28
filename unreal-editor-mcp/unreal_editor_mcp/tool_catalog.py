"""Ordered assembly of the released Unreal Editor MCP tool catalog."""

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
LARGE_TOOLS: Final = (*TOOLS, EDITOR_LIFECYCLE_TOOL)


def tools_for_mode(mode: str) -> tuple[dict[str, object], ...]:
    if mode == "default":
        return TOOLS
    if mode == "large":
        return LARGE_TOOLS
    raise ValueError("Unsupported tool mode")


TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
