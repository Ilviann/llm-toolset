"""Compatibility facade for typed Blueprint scenario handoffs."""

from __future__ import annotations

from collections.abc import Mapping

from unreal_editor_mcp.bridge import UnrealBridge
from unreal_editor_mcp.project import ProjectLayout

from .blueprint_fixture_preparation import prepare_blueprint_scenario as _prepare
from .blueprint_graph_editing import author_blueprint_scenario as _author
from .blueprint_restart_verification import verify_restarted_blueprints as _verify
from .blueprint_state import BlueprintFixtureState, BlueprintScenarioState


def prepare_blueprint_scenario(bridge: UnrealBridge) -> BlueprintFixtureState:
    return BlueprintFixtureState.from_mapping(_prepare(bridge))


def author_blueprint_scenario(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    capabilities: dict[str, object],
    created: dict[str, object],
    phase_fourteen_families: dict[str, dict[str, object]],
    phase_fifteen_game_instance: dict[str, object],
) -> BlueprintScenarioState:
    return BlueprintScenarioState.from_mapping(_author(
        bridge,
        layout,
        capabilities,
        created,
        phase_fourteen_families,
        phase_fifteen_game_instance,
    ))


def verify_restarted_blueprints(
    bridge: UnrealBridge,
    layout: ProjectLayout,
    phase_two_loaded_snapshot: str,
    phase_two_loaded_inspection: dict[str, object],
    phase_fourteen_families: dict[str, dict[str, object]],
    phase_fifteen_game_instance: dict[str, object],
    scenario: Mapping[str, object],
) -> None:
    _verify(
        bridge,
        layout,
        phase_two_loaded_snapshot,
        phase_two_loaded_inspection,
        phase_fourteen_families,
        phase_fifteen_game_instance,
        scenario,  # type: ignore[arg-type]
    )


__all__ = [
    "BlueprintFixtureState",
    "BlueprintScenarioState",
    "author_blueprint_scenario",
    "prepare_blueprint_scenario",
    "verify_restarted_blueprints",
]
