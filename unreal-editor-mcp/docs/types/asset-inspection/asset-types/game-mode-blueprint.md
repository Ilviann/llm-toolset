# GameMode Blueprint

This family covers Blueprints whose generated classes derive from `AGameModeBase`. It distinguishes the simpler `AGameModeBase` rules framework from `AGameMode`, which adds Unreal's built-in match-state machine.

## Root response

An `AGameMode`-derived asset has a cumulative response such as:

```yaml
asset:
  type: game_mode_blueprint
  parent_type: /Script/Engine.GameMode
actor:
  world_presence: server_only
  lifetime: world
  placeable: false
  replicated_to_clients: false
  tags:
    kind: array
    item_type: name
    count: 0
    selector: properties/actor/tags
  tick:
    can_ever_tick: true
    start_enabled: true
game_mode_base:
  classes:
    game_session: /Script/Engine.GameSession
    game_state: /Script/Engine.GameState
    player_controller: /Script/Engine.PlayerController
    player_state: /Script/Engine.PlayerState
    hud: /Script/Engine.HUD
    default_pawn: /Script/Engine.DefaultPawn
    spectator_pawn: /Script/Engine.SpectatorPawn
    replay_spectator_player_controller: /Script/Engine.PlayerController
    server_stat_replicator: /Script/Engine.ServerStatReplicator
  seamless_travel: true
  start_players_as_spectators: false
  pauseable: true
  replication_system: default
  default_player_name: Player
game_mode:
  delayed_start: true
  minimum_respawn_delay_seconds: 1.0
  inactive_player_state:
    lifespan_seconds: 300.0
    maximum_count: 16
  dedicated_server_replays: false
  engine_message_class: /Script/Engine.EngineMessage
variables:
  - {name: ScoreLimit, type: int, value: 20}
event_graphs:
  - name: EventGraph
    events: [BeginPlay, OnPostLogin, OnLogout, OnSetMatchState]
functions:
  - name: ReadyToStartMatch
    kind: override
    declared_by: /Script/Engine.GameMode
    inputs: {}
    outputs: {ReturnValue: bool}
macros: []
selectors: [event_graphs, events, functions, properties]
```

An asset derived from `AGameModeBase` but not `AGameMode` uses `type: game_mode_base_blueprint`, includes `actor` and `game_mode_base`, and omits the `game_mode` block and match-state callbacks.

## Root property rules

- `parent_type` is the exact immediate native or Blueprint-generated parent class path. `type` describes the nearest supported Unreal framework family, not merely the immediate parent name.
- The inherited `actor` block includes important semantics that still apply to GameMode: server-only world presence, world lifetime, non-placeability, absence from clients, effective Actor tags, and effective tick policy. Omit irrelevant Actor transform, rendering, collision, client replication-tuning, and level-instance fields.
- `game_mode_base.classes` contains the effective class defaults that determine the session, replicated GameState, controllers, PlayerStates, HUD, player and spectator pawns, replay controller, and server statistics helper.
- The remaining `game_mode_base` fields report effective rules for seamless travel, initial spectator spawning, pausing, net-driver replication system, and the default player name.
- The `game_mode` block exists only for `AGameMode` descendants. It reports the editable defaults governing match start, respawn delay, inactive PlayerState retention, dedicated-server replay handling, and engine messages.
- Blueprint variables contain locally declared variables with their K2 types and effective class-default values.
- Framework event implementations and function overrides are listed only when the asset actually implements them. An override entry has `kind: override` and the exact framework `declared_by` class; user-declared functions use `kind: function`.
- Components locally owned by the Blueprint follow the shared Actor component-summary rules. Native or inherited components are identified as inherited and are not presented as asset-owned declarations.
- Do not claim that the asset is the project's or world's active GameMode. Selection precedence through URL options, World Settings, and project settings is external to the asset.

## Excluded runtime state

Do not return a live world's options string, GameSession or GameState instance, current match state, player or spectator counts, travelling-player count, inactive-player array, pausers, current controllers, spawn points, or other runtime world objects. These are transient observations rather than logic or defaults encapsulated by the asset. Match-state transitions remain analyzable through implemented events, function overrides, and graph call sites.

## Framework inheritance

- `AGameModeBase` responses include the important inherited Actor semantics and the important GameModeBase defaults.
- `AGameMode` responses cumulatively include the Actor, GameModeBase, and GameMode semantic blocks.
- A supported engine, project, or plugin subclass adds its important semantic properties without removing inherited blocks. Emit each property once with exact declaring-type provenance.
- A subclass without a dedicated adapter may add bounded safe reflected class-default properties declared by that subclass. Skip transient, editor-only, delegate, recursive object, bulk, and media-bearing fields.

## Selectors

- `event_graphs/<graph>` — complete semantic graph.
- `events/<graph>/<event>` — the selected login, logout, match, travel, lifecycle, or custom-event execution slice plus required data dependencies.
- `functions/<function>` — a user function or framework override with signature, declaring class, local variables, callable table, semantic nodes, and inline outgoing links.
- `macros/<macro>` — present only when the asset defines at least one macro.
- `properties/actor/tags` — zero-based pages of effective Actor tags, including a valid empty page when the collection is empty.

The root advertises only non-empty selector namespaces. Inherited framework functions that the asset does not override are not selector targets; calls to them are described through the selected graph's deduplicated callable table.

## Implementation implications

- The existing family policy already distinguishes `game_mode_base` from `game_mode` and deeply inspects both through the Actor path.
- Current reflected defaults already include representative GameModeBase and GameMode fields. `asset_inspect` needs a curated cumulative semantic adapter and inherited-framework provenance, not a new companion.
- Current action-catalog behavior remains authoritative for which framework events and overrides can actually exist in each graph.

## Open questions

- None at the current requirements layer. The distinct `game_mode_base_blueprint` and `game_mode_blueprint` type values are accepted.
