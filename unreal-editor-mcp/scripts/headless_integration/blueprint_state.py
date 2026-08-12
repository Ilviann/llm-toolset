"""Typed handoff state for Blueprint lifecycle scenarios."""

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
class BlueprintFamilyState(_StateMapping):
    phase_fourteen_families: object
    phase_fifteen_game_instance: object

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
        return cls(snapshot, value["phase_two_loaded_inspection"], value["created"],
                   BlueprintFamilyState(value["phase_fourteen_families"],
                                        value["phase_fifteen_game_instance"]))

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
    event_graph_id: str
    graph_node_id: str

    @classmethod
    def from_mapping(cls, value: Mapping[str, object]) -> "BlueprintScenarioState":
        fields = tuple(value[name] for name in (
            "assigned_game_instance_class", "assigned_game_mode_class", "created_snapshot",
            "event_graph_id", "graph_node_id",
        ))
        if not all(isinstance(item, str) for item in fields):
            raise TypeError("Blueprint scenario identities must be strings")
        return cls(*fields)

    def _values(self) -> dict[str, object]:
        return self.__dict__.copy()
