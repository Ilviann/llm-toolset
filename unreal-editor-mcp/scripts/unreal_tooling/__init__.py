"""Shared, Unreal-local primitives for repository support tools."""

from .engines import (
    ENGINE_ROOT_ENV,
    MAX_ENGINE_VERSION_BYTES,
    MIN_UNREAL_ENGINE_VERSION,
    XCODE_APP_ENV,
    XCODE_DEVELOPER_RELATIVE,
    EngineInstallation,
    configure_build_environment,
    read_engine_version,
    validate_engine_installation,
)
from .errors import ToolingError
from .paths import is_reparse_point, is_within, read_json_object, resolved
from .plugins import (
    AI_PLUGIN,
    APPLICATION_ROOT,
    BASE_PLUGIN,
    COMMONUI_PLUGIN,
    ENHANCED_INPUT_PLUGIN,
    FIXTURE_PLUGIN,
    GAS_PLUGIN,
    PLUGINS,
    WORKSPACE_ROOT,
    PluginIdentity,
)

__all__ = [
    "AI_PLUGIN",
    "APPLICATION_ROOT",
    "BASE_PLUGIN",
    "COMMONUI_PLUGIN",
    "ENHANCED_INPUT_PLUGIN",
    "ENGINE_ROOT_ENV",
    "FIXTURE_PLUGIN",
    "GAS_PLUGIN",
    "MAX_ENGINE_VERSION_BYTES",
    "MIN_UNREAL_ENGINE_VERSION",
    "XCODE_APP_ENV",
    "XCODE_DEVELOPER_RELATIVE",
    "PLUGINS",
    "WORKSPACE_ROOT",
    "EngineInstallation",
    "PluginIdentity",
    "ToolingError",
    "configure_build_environment",
    "is_reparse_point",
    "is_within",
    "read_engine_version",
    "read_json_object",
    "resolved",
    "validate_engine_installation",
]
