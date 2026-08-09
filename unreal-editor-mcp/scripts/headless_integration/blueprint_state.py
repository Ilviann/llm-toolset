"""Typed handoff state for Blueprint authoring and restart verification."""

from __future__ import annotations

from collections.abc import Iterator, Mapping
from dataclasses import dataclass


class _StateMapping(Mapping[str, object]):
    def _values(self) -> dict[str, object]:
        raise NotImplementedError

    def __getitem__(self, key: str) -> object:
        return self._values()[key]

    def __iter__(self) -> Iterator[str]:
        return iter(self._values())

    def __len__(self) -> int:
        return len(self._values())


@dataclass(frozen=True)
class BlueprintDeclarationState(_StateMapping):
    function_id: object
    local_id: object
    macro_id: object
    custom_event_id: object
    member_id: object
    notify_id: object

    def _values(self) -> dict[str, object]:
        return self.__dict__.copy()


@dataclass(frozen=True)
class BlueprintFamilyState(_StateMapping):
    phase_fourteen_families: object
    phase_fifteen_game_instance: object

    def _values(self) -> dict[str, object]:
        return self.__dict__.copy()


@dataclass(frozen=True)
class BlueprintReplacementState(_StateMapping):
    custom_replace_node_id: object
    macro_replace_node_id: object
    print_node_id: object

    def _values(self) -> dict[str, object]:
        return self.__dict__.copy()


@dataclass(frozen=True)
class BlueprintNodeState(_StateMapping):
    begin_play_node_id: object
    conversion_node_id: object
    custom_event_exec_source_node_id: object
    function_node_id: object
    graph_node_id: object
    literal_node_id: object
    operator_node_id: object
    setter_node_id: object
    shared_print_node_id: object
    temporary_node_id: object

    def _values(self) -> dict[str, object]:
        return self.__dict__.copy()


@dataclass(frozen=True)
class BlueprintPinState(_StateMapping):
    custom_event_exec_pin_id: object
    graph_pin_ids: object
    setter_exec_pin_id: object
    setter_value_pin_id: object

    def _values(self) -> dict[str, object]:
        return self.__dict__.copy()


@dataclass(frozen=True)
class BlueprintFixtureState(_StateMapping):
    phase_two_loaded_snapshot: str
    phase_two_loaded_inspection: object
    created: object
    families: BlueprintFamilyState

    @classmethod
    def from_mapping(cls, value: Mapping[str, object]) -> "BlueprintFixtureState":
        snapshot = value["phase_two_loaded_snapshot"]
        if not isinstance(snapshot, str):
            raise TypeError("phase_two_loaded_snapshot must be a string")
        return cls(
            snapshot,
            value["phase_two_loaded_inspection"],
            value["created"],
            BlueprintFamilyState(value["phase_fourteen_families"], value["phase_fifteen_game_instance"]),
        )

    def _values(self) -> dict[str, object]:
        return {
            "phase_two_loaded_snapshot": self.phase_two_loaded_snapshot,
            "phase_two_loaded_inspection": self.phase_two_loaded_inspection,
            "created": self.created,
            **self.families._values(),
        }


@dataclass(frozen=True)
class BlueprintScenarioState(_StateMapping):
    assigned_game_instance_class: str
    assigned_game_mode_class: str
    created_snapshot: str
    declarations: BlueprintDeclarationState
    replacements: BlueprintReplacementState
    nodes: BlueprintNodeState
    pins: BlueprintPinState

    @classmethod
    def from_mapping(cls, value: Mapping[str, object]) -> "BlueprintScenarioState":
        assigned_instance = value["assigned_game_instance_class"]
        assigned_mode = value["assigned_game_mode_class"]
        snapshot = value["created_snapshot"]
        if not all(isinstance(item, str) for item in (assigned_instance, assigned_mode, snapshot)):
            raise TypeError("assigned classes and created_snapshot must be strings")
        return cls(
            assigned_instance,
            assigned_mode,
            snapshot,
            BlueprintDeclarationState(*(value[name] for name in (
                "function_id", "local_id", "macro_id", "custom_event_id", "member_id", "notify_id",
            ))),
            BlueprintReplacementState(*(value[name] for name in (
                "custom_replace_node_id", "macro_replace_node_id", "print_node_id",
            ))),
            BlueprintNodeState(*(value[name] for name in (
                "begin_play_node_id", "conversion_node_id", "custom_event_exec_source_node_id",
                "function_node_id", "graph_node_id", "literal_node_id", "operator_node_id",
                "setter_node_id", "shared_print_node_id", "temporary_node_id",
            ))),
            BlueprintPinState(*(value[name] for name in (
                "custom_event_exec_pin_id", "graph_pin_ids", "setter_exec_pin_id", "setter_value_pin_id",
            ))),
        )

    def _values(self) -> dict[str, object]:
        return {
            "assigned_game_instance_class": self.assigned_game_instance_class,
            "assigned_game_mode_class": self.assigned_game_mode_class,
            "created_snapshot": self.created_snapshot,
            **self.declarations._values(),
            **self.replacements._values(),
            **self.nodes._values(),
            **self.pins._values(),
        }
