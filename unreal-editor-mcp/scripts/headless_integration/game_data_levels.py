"""Compatibility facade for decomposed game-data and level scenarios."""

from .game_data import author_phase_seventeen_game_data, collect_game_data, verify_restarted_game_data_and_level
from .level_editing import author_level_edit_scenario, verify_restarted_level_edit
from .level_management import manage_disposable_level, verify_restarted_level_deletion
from .level_opening import open_acceptance_level

__all__ = [
    "author_level_edit_scenario",
    "author_phase_seventeen_game_data",
    "collect_game_data",
    "manage_disposable_level",
    "open_acceptance_level",
    "verify_restarted_game_data_and_level",
    "verify_restarted_level_deletion",
    "verify_restarted_level_edit",
]
