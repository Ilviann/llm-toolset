"""Compatibility facade for focused companion scenarios."""

from unreal_editor_mcp.bridge import UnrealBridge

from .commonui_scenario import verify_commonui_inspection
from .companion_admission import verify_companion_admission
from .gas_scenario import verify_gas_inspection
from .test_companion_scenario import verify_test_companion


def verify_companion_scenario(bridge: UnrealBridge, capabilities: dict[str, object]) -> None:
    verify_companion_admission(capabilities)
    verify_commonui_inspection(bridge)
    verify_gas_inspection(bridge)
    verify_test_companion(bridge)


__all__ = ["verify_companion_scenario"]
