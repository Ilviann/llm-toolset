# GameState Blueprint

This family covers Blueprints whose generated classes derive from `AGameStateBase`. It distinguishes `AGameStateBase` from `AGameState`, which adds the replicated counterpart of `AGameMode`'s match-state machine.

## Root response

An `AGameState`-derived asset has a cumulative response such as:

```yaml
asset:
  type: game_state_blueprint
  parent_type: /Script/Engine.GameState
actor:
  world_presence: server_and_clients
  lifetime: world
  placeable: false
  tags:
    kind: array
    item_type: name
    count: 0
    selector: properties/actor/tags
  replication:
    enabled: true
    always_relevant: true
    movement: false
    net_load_on_client: false
    priority: 10.0
  tick:
    can_ever_tick: false
game_state_base:
  server_world_time:
    synchronized: true
    update_frequency_seconds: 0.1
  runtime_contract:
    game_mode_class: replicated_from_authority
    authority_game_mode: server_only
    spectator_class: replicated_from_authority
    player_states: maintained_on_server_and_clients
    begun_play: replicated_from_authority
game_state:
  runtime_contract:
    match_state: replicated_from_authority
    previous_match_state: local_transition_context
    elapsed_time_seconds: replicated_from_authority
variables:
  - name: TeamScores
    type: map<int, int>
    value: {}
    replication: replicated
event_graphs:
  - name: EventGraph
    events: [BeginPlay, OnRep_TeamScores]
functions:
  - name: GetLeadingTeam
    kind: function
    inputs: {}
    outputs: {TeamId: int}
macros: []
selectors: [event_graphs, events, functions, properties]
```

An asset derived from `AGameStateBase` but not `AGameState` uses `type: game_state_base_blueprint`, includes `actor` and `game_state_base`, and omits the `game_state` match-state block.

## Root property rules

- `parent_type` is the exact immediate native or Blueprint-generated parent. `type` records the nearest supported framework family as `game_state_base_blueprint` or `game_state_blueprint`.
- The inherited `actor` block reports the effective class defaults important to GameState: server/client presence, world lifetime, non-placeability, tags, replication, relevancy, movement replication, client loading, network priority, and tick policy. Omit transforms, rendering, collision, input, and other irrelevant Actor fields.
- `game_state_base.server_world_time.update_frequency_seconds` is the effective class-default `ServerWorldTimeSecondsUpdateFrequency`. `synchronized` documents the framework role of the property.
- `game_state_base.runtime_contract` identifies important inherited runtime properties without pretending that a class asset has live values. It records their replication or authority semantics instead of serializing a current GameMode, spectator class, PlayerState array, or begun-play value.
- The `game_state.runtime_contract` block exists only for `AGameState` descendants and describes its match state, previous transition state, and elapsed match time. These are runtime properties and therefore have semantic policies rather than current values.
- Blueprint variables contain locally declared variables with K2 types, effective class-default values, and replication or RepNotify policy where applicable.
- Event graphs list only event and RepNotify nodes actually implemented in the asset. Functions list only asset-owned functions and genuine Blueprint framework overrides; do not synthesize graphs for native framework methods that Blueprint cannot override.
- Locally owned Blueprint components use the shared Actor component-summary rules. Inherited and native components retain their ownership identity.

## Excluded runtime state

Do not return current GameMode or spectator classes, the authority GameMode instance, PlayerState objects, current server-time delta, current begun-play state, current or previous match state, current elapsed time, timers, or live replicated values. A future runtime-inspection tool may expose observations, but `asset_inspect` describes the asset's defaults, declarations, contracts, and logic.

## Framework inheritance

- `AGameStateBase` responses include important Actor semantics and GameStateBase properties and contracts.
- `AGameState` responses cumulatively add the GameState match-state contract.
- Supported engine, project, or plugin subclasses add important properties while retaining inherited blocks. Emit each property once with exact declaring-type provenance.
- Unknown subclasses may add bounded safe reflected class defaults declared by that subclass. Skip transient values, delegates, recursive object graphs, editor-only data, and media-bearing fields.

## Selectors

- `event_graphs/<graph>` — complete semantic graph.
- `events/<graph>/<event>` — selected lifecycle, replication-notification, or custom-event slice with required data dependencies.
- `functions/<function>` — signature, function kind and declaring class, local variables, callable table, semantic nodes, and inline outgoing links.
- `macros/<macro>` — present only when at least one macro exists.
- `properties/actor/tags` — zero-based pages of effective Actor tags.

The root advertises only non-empty namespaces. Calls to inherited framework functions such as `GetServerWorldTimeSeconds`, `HasBegunPlay`, `HasMatchStarted`, and `HasMatchEnded` appear in a selected graph's callable table rather than becoming selector targets unless the asset owns an override graph.

## Implementation implications

- The existing family policy already distinguishes `game_state_base` from `game_state` and inspects both through the Actor path.
- Existing reflected-default support includes `ServerWorldTimeSecondsUpdateFrequency` and safe inherited Actor defaults. The new work is primarily the cumulative semantic projection and explicit separation of class defaults from runtime contracts.
- No companion is required.

## Open questions

- None at the current requirements layer. Important runtime-only framework properties remain represented through fixed `runtime_contract` semantics; a later response filter may optionally suppress them.
