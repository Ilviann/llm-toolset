"""Compatibility exports for companion entries in the asset-family catalog."""

from .asset_family_catalog import (
    COMPANION_API_VERSION,
    EXTENSION_SCHEMA_REVISION,
    ASSET_FAMILY_CATALOG,
    compose_companion_capabilities,
    compose_extension_tools,
)


EXTENSION_CATALOG = {
    extension_id: {
        "schema_revision": entry.schema_revision,
        "contributions": {
            contribution.key: contribution.input_schema
            for contribution in entry.contributions
        },
        "integrated_sections": {
            sections.key: sections.sections
            for sections in entry.integrated_sections
        },
    }
    for extension_id, entry in ASSET_FAMILY_CATALOG.companions.items()
}
