# GameInstance Blueprint

This family covers a Blueprint whose generated class derives from `UGameInstance`. It is a non-Actor, non-replicated lifecycle object that persists across map travel for one game instance.

## Root response

```yaml
asset:
  type: game_instance_blueprint
  parent_type: /Script/Engine.GameInstance
game_instance:
  lifetime: game_instance_session
  persists_across_level_travel: true
  network_scope: local_game_instance
  replicated: false
variables:
  - {name: SelectedCharacter, type: name, value: None}
  - {name: SessionPreferences, type: struct</Script/MyGame.SessionPreferences>, value: "(...)"}
event_graphs:
  - name: EventGraph
    events: [Init, Shutdown, NetworkError, TravelError]
functions:
  - name: SaveSessionPreferences
    inputs: {}
    outputs: {Succeeded: bool}
macros: []
selectors: [event_graphs, events, functions]
```

## Root property rules

- `type` is `game_instance_blueprint`; `parent_type` is the exact immediate native or Blueprint-generated parent class path.
- The `game_instance` block states fixed semantics that materially affect reasoning: the object lives for one game-instance session, survives level travel, exists independently in each local game instance, and is not replicated between server and clients.
- `variables` contains locally declared custom Blueprint variables with K2 type and effective class-default value. Do not serialize runtime values from a live GameInstance.
- Event graphs list only implemented event nodes. Normalize the native callback identities `ReceiveInit`, `ReceiveShutdown`, `HandleNetworkError`, and `HandleTravelError` to the editor-facing semantic names `Init`, `Shutdown`, `NetworkError`, and `TravelError`, while retaining exact native identities internally.
- Functions and macros list only asset-owned declarations and follow the shared deduplicated signature rules.
- Do not output local-player arrays, world context, online-session instances, timer-manager state, input-device delegates, referenced worlds, or other transient runtime state.
- Do not claim that the inspected class is configured as the project's active GameInstance; that setting is external project configuration rather than data encapsulated by the asset.

## Framework inheritance

- A derived GameInstance response remains cumulative: it contains the important `UGameInstance` semantics and properties plus important properties introduced by every supported derived framework type in its inheritance chain.
- Emit each property once, grouped by or annotated with its exact declaring type. Do not reduce the response to only the most-derived class delta.
- Project/plugin subclasses without dedicated adapters may add bounded reflected properties declared by that subclass when supported by the shared property codec; these additions do not remove inherited framework properties.
- Skip transient runtime references, delegates, recursive UObject graphs, editor-only state, and unsupported or media-bearing values.

## Selectors

- `event_graphs/<graph>` — complete semantic graph.
- `events/<graph>/<event>` — reachable lifecycle or error-handler slice plus required pure/data dependencies.
- `functions/<function>` — signature, local variables, callable table, semantic nodes, and inline outgoing links.
- `macros/<macro>` — present only when the asset defines at least one macro.

The root `selectors` list contains only non-empty namespaces.

## Implementation implications

- The existing base family policy, inspector, and tests already support GameInstance Blueprints, callbacks, variables, functions, macros, graphs, and component rejection.
- `asset_inspect` needs a semantic adapter over those existing records; it does not need a new family, companion, or direct runtime GameInstance inspection path.

## Open questions

- None at the current requirements layer. The fixed `game_instance` lifetime and network block is accepted as useful semantic context.
