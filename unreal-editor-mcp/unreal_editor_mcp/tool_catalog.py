"""Compatibility exports for the static Python asset-family catalog."""

from __future__ import annotations

from typing import Final

from .asset_family_catalog import (
    ASSET_FAMILY_CATALOG,
    LIFECYCLE_HANDLER,
    MUTATION_ACCESS,
    READ_ACCESS,
)


SUPPORTED_PROTOCOLS: Final = ("2024-11-05", "2025-03-26", "2025-06-18")
LATEST_PROTOCOL: Final = SUPPORTED_PROTOCOLS[-1]

TOOLS_WITH_LIFECYCLE: Final = ASSET_FAMILY_CATALOG.compose(
    writable=True,
    lifecycle_enabled=True,
).tools
TOOLS: Final = tuple(tool for tool in TOOLS_WITH_LIFECYCLE if tool["name"] != "editor_lifecycle")
READONLY_TOOL_NAMES: Final = frozenset(
    name for name, publication in ASSET_FAMILY_CATALOG.publications.items()
    if publication.access == READ_ACCESS and publication.handler != LIFECYCLE_HANDLER
)
WRITABLE_TOOL_NAMES: Final = frozenset(
    name for name, publication in ASSET_FAMILY_CATALOG.publications.items()
    if publication.access == MUTATION_ACCESS
)
READONLY_TOOLS: Final = ASSET_FAMILY_CATALOG.compose(
    writable=False,
    lifecycle_enabled=False,
).tools
READONLY_TOOLS_WITH_LIFECYCLE: Final = ASSET_FAMILY_CATALOG.compose(
    writable=False,
    lifecycle_enabled=True,
).tools


def tools_for_configuration(
    *,
    writable: bool,
    lifecycle_enabled: bool,
) -> tuple[dict[str, object], ...]:
    """Return the deterministic public catalog for immutable startup access."""
    return ASSET_FAMILY_CATALOG.compose(
        writable=writable,
        lifecycle_enabled=lifecycle_enabled,
    ).tools


TOOL_BY_NAME: Final = {tool["name"]: tool for tool in TOOLS}
