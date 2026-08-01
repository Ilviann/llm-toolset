"""Level discovery, inspection, and opening tools."""

from __future__ import annotations

from typing import Final

from .schemas import _OPERATION_ID, _PATH, _SNAPSHOT_ID

_ACTOR_ID = {
    "type": "string",
    "minLength": 73,
    "maxLength": 73,
    "pattern": "^[0-9a-f]{40}:[0-9a-f]{32}$",
}
_COMPONENT_ID = {
    "type": "string",
    "minLength": 32,
    "maxLength": 32,
    "pattern": "^[0-9a-f]{32}$",
}
_VECTOR = {
    "type": "object",
    "properties": {
        "x": {"type": "number", "minimum": -1000000000, "maximum": 1000000000},
        "y": {"type": "number", "minimum": -1000000000, "maximum": 1000000000},
        "z": {"type": "number", "minimum": -1000000000, "maximum": 1000000000},
    },
    "required": ["x", "y", "z"],
    "additionalProperties": False,
}
_REGION = {
    "type": "object",
    "properties": {"min": _VECTOR, "max": _VECTOR},
    "required": ["min", "max"],
    "additionalProperties": False,
}
_ACTOR_FILTERS = {
    "type": "object",
    "properties": {
        "actor_id": _ACTOR_ID,
        "label": {"type": "string", "minLength": 1, "maxLength": 128},
        "class_path": _PATH,
        "tag": {"type": "string", "minLength": 1, "maxLength": 128},
        "folder": {"type": "string", "minLength": 1, "maxLength": 512},
        "data_layer": {"type": "string", "minLength": 1, "maxLength": 512},
        "loaded": {"type": "boolean"},
        "region": _REGION,
    },
    "additionalProperties": False,
}
_PROPERTY_NAMES = {
    "type": "array",
    "minItems": 1,
    "maxItems": 32,
    "uniqueItems": True,
    "items": {
        "type": "string",
        "minLength": 1,
        "maxLength": 128,
        "pattern": r"^[^.\\]+$",
    },
}
_LEVEL_SETTING = {
    "type": "object",
    "properties": {
        "property_name": {
            "type": "string",
            "enum": [
                "DefaultGameMode", "bEnableWorldBoundsChecks", "bEnableAISystem",
                "bUseClientSideLevelStreamingVolumes", "bEnableWorldOriginRebasing",
                "bGlobalGravitySet", "GlobalGravityZ", "WorldToMeters", "KillZ",
                "KillZDamageType", "DefaultPhysicsVolumeClass",
                "PhysicsCollisionHandlerClass", "DefaultColorScale",
                "PackedLightAndShadowMapTextureSize", "bForceNoPrecomputedLighting",
                "DefaultMaxDistanceFieldOcclusionDistance",
            ],
        },
        "value": {
            "oneOf": [
                {"type": "boolean"},
                {"type": "number", "minimum": -1000000000, "maximum": 1000000000},
                {"type": "string", "maxLength": 4096},
                {
                    "type": "array", "maxItems": 64, "uniqueItems": True,
                    "items": {"type": "string", "minLength": 1, "maxLength": 128},
                },
            ]
        },
    },
    "required": ["property_name", "value"],
    "additionalProperties": False,
}
_LEVEL_SETTINGS = {
    "type": "array",
    "minItems": 1,
    "maxItems": 16,
    "items": _LEVEL_SETTING,
}

LEVEL_TOOLS: Final = (
    {
        "name": "level_inspect",
        "description": "Discover maps or inspect the current map, bounded actor descriptors, and exact actor/component instance properties.",
        "inputSchema": {
            "oneOf": [
                {
                    "type": "object",
                    "properties": {
                        "mode": {"const": "discover"},
                        "package_path": {
                            "type": "string",
                            "minLength": 1,
                            "maxLength": 512,
                            "pattern": r"^(?!.*\.\.)/[^\\]*$",
                        },
                        "asset_name": {"type": "string", "minLength": 1, "maxLength": 128},
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["mode"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {"mode": {"const": "current"}},
                    "required": ["mode"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "mode": {"const": "actors"},
                        "map_id": _SNAPSHOT_ID,
                        "expected_snapshot": _SNAPSHOT_ID,
                        "filters": _ACTOR_FILTERS,
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["mode", "map_id", "expected_snapshot"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "mode": {"const": "actor"},
                        "map_id": _SNAPSHOT_ID,
                        "expected_snapshot": _SNAPSHOT_ID,
                        "actor_id": _ACTOR_ID,
                        "property_names": _PROPERTY_NAMES,
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["mode", "map_id", "expected_snapshot", "actor_id"],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "mode": {"const": "component"},
                        "map_id": _SNAPSHOT_ID,
                        "expected_snapshot": _SNAPSHOT_ID,
                        "actor_id": _ACTOR_ID,
                        "component_id": _COMPONENT_ID,
                        "property_names": _PROPERTY_NAMES,
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": [
                        "mode", "map_id", "expected_snapshot", "actor_id", "component_id",
                    ],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "cursor": _OPERATION_ID,
                        "page_size": {"type": "integer", "minimum": 1, "maximum": 100},
                    },
                    "required": ["cursor"],
                    "additionalProperties": False,
                },
            ]
        },
    },
    {
        "name": "level_open",
        "description": "Safely open one exact mounted World asset without implicitly saving, discarding, or prompting.",
        "inputSchema": {
            "type": "object",
            "properties": {
                "operation_id": _OPERATION_ID,
                "map_path": _PATH,
            },
            "required": ["operation_id", "map_path"],
            "additionalProperties": False,
        },
    },
    {
        "name": "level_manage",
        "description": "Create or configure and persist one exact project map with explicit current-map safety and bounded World Settings.",
        "inputSchema": {
            "oneOf": [
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID,
                        "operation": {"const": "create"},
                        "destination_path": _PATH,
                        "source": {
                            "type": "object",
                            "properties": {"kind": {"const": "blank"}},
                            "required": ["kind"],
                            "additionalProperties": False,
                        },
                        "creation_options": {
                            "type": "object",
                            "properties": {
                                "world_partition": {"type": "boolean"},
                                "world_partition_streaming": {"type": "boolean"},
                                "external_actors": {"type": "boolean"},
                            },
                            "required": ["world_partition", "world_partition_streaming", "external_actors"],
                            "additionalProperties": False,
                        },
                        "settings": _LEVEL_SETTINGS,
                        "open_after_create": {"type": "boolean"},
                        "expected_current_snapshot": _SNAPSHOT_ID,
                    },
                    "required": [
                        "operation_id", "operation", "destination_path", "source",
                        "creation_options", "open_after_create", "expected_current_snapshot",
                    ],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID,
                        "operation": {"const": "create"},
                        "destination_path": _PATH,
                        "source": {
                            "type": "object",
                            "properties": {"kind": {"const": "template"}, "map_path": _PATH},
                            "required": ["kind", "map_path"],
                            "additionalProperties": False,
                        },
                        "settings": _LEVEL_SETTINGS,
                        "open_after_create": {"type": "boolean"},
                        "expected_current_snapshot": _SNAPSHOT_ID,
                    },
                    "required": [
                        "operation_id", "operation", "destination_path", "source",
                        "open_after_create", "expected_current_snapshot",
                    ],
                    "additionalProperties": False,
                },
                {
                    "type": "object",
                    "properties": {
                        "operation_id": _OPERATION_ID,
                        "operation": {"const": "configure"},
                        "map_path": _PATH,
                        "expected_current_snapshot": _SNAPSHOT_ID,
                        "settings": _LEVEL_SETTINGS,
                        "reload_after_save": {"type": "boolean"},
                    },
                    "required": [
                        "operation_id", "operation", "map_path",
                        "expected_current_snapshot", "settings", "reload_after_save",
                    ],
                    "additionalProperties": False,
                },
            ]
        },
    },
)
