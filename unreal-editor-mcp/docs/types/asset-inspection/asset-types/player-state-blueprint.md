# PlayerState Blueprint

This family covers Blueprints whose generated classes derive from `APlayerState`. The response cumulatively describes the important inherited Actor properties, PlayerState class defaults, replicated runtime contracts, and asset-owned logic.

## Root response

```yaml
asset:
  type: player_state_blueprint
  parent_type: /Script/Engine.PlayerState
actor:
  world_presence: server_and_all_clients
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
    update_frequency_hz: 1.0
player_state:
  update_replicated_ping: true
  use_custom_player_names: false
  engine_message_class: /Script/Engine.EngineMessage
  session_name: GameSession
  runtime_contract:
    score: {authority: server, replication: all_clients, notify: OnRep_Score}
    player_id: {authority: server, replication: all_clients, notify: OnRep_PlayerId}
    player_name: {authority: server, replication: all_clients, notify: OnRep_PlayerName}
    unique_net_id: {authority: server, replication: all_clients, notify: OnRep_UniqueId}
    compressed_ping: {authority: server, replication: all_clients, unit: milliseconds_divided_by_4}
    spectator: {authority: server, replication: all_clients}
    only_spectator: {authority: server, replication: all_clients}
    bot: {authority: server, replication: all_clients}
    inactive: {authority: server, replication: all_clients, notify: OnRep_bIsInactive}
    from_previous_level: {authority: server, replication: all_clients}
    start_time_seconds: {authority: server, replication: all_clients}
    pawn: runtime_reference
  transfer_contract:
    seamless_travel: CopyProperties
    reconnection: OverrideWith
variables:
  - name: TeamId
    type: int
    value: 0
    replication: replicated_notify
    notify: OnRep_TeamId
event_graphs:
  - name: EventGraph
    events: [BeginPlay, CopyProperties, OverrideWith, OnRep_TeamId]
functions:
  - name: AddScore
    kind: function
    inputs: {Amount: float}
    outputs: {}
macros: []
selectors: [event_graphs, events, functions, properties]
```

## Root property rules

- `parent_type` is the exact immediate native or Blueprint-generated parent. `type` is `player_state_blueprint` for every `APlayerState` descendant.
- The inherited `actor` block reports the effective world presence, non-placeability, tags, replication, relevancy, movement replication, client loading, and network update frequency. Omit transforms, collision, rendering, input, and other irrelevant Actor fields.
- `update_replicated_ping` is the effective `bShouldUpdateReplicatedPing` class default. It matters because disabling automatic compressed-ping replication can reduce cost in games with many always-relevant PlayerStates.
- `use_custom_player_names`, `engine_message_class`, and `session_name` report effective framework defaults that change player-name lookup, localized framework messages, or session registration behavior.
- `runtime_contract` lists important inherited PlayerState properties while deliberately omitting live player values. Each record states authority, replication, RepNotify identity, units, or another semantic role as applicable.
- `transfer_contract` identifies the two Blueprint extension points used to move custom state: `CopyProperties` into the new PlayerState during seamless travel and `OverrideWith` from an inactive PlayerState during reconnection.
- Blueprint variables contain locally declared defaults plus replication and RepNotify policies. Event graphs list only lifecycle, RepNotify, and custom event nodes actually implemented in the asset.
- Functions, macros, RPCs, and locally owned components follow the shared Actor Blueprint rules.

## Excluded runtime state

Do not return actual player names or IDs, unique online identifiers, network addresses, score, ping, spectator/bot/inactive flags, start time, current Pawn or Controller, session membership, inactive duplicates, replication history, or live delegate bindings. Opaque unique IDs must never be expanded into provider-specific internals.

## Framework inheritance

- The response includes important Actor semantics and all important PlayerState configuration and runtime contracts.
- Supported engine, project, or plugin subclasses add important semantic properties without removing inherited blocks. Emit each property once with exact declaring-type provenance.
- Unknown subclasses may add bounded safe reflected class defaults declared by the subclass. Skip transient state, delegates, recursive object graphs, editor-only data, secrets, opaque provider internals, and media-bearing fields.

## Selectors

- `event_graphs/<graph>` — complete semantic graph.
- `events/<graph>/<event>` — selected lifecycle, property-transfer, RepNotify, RPC, or custom-event slice plus required data dependencies.
- `functions/<function>` — signature, function or override kind, declaring class, local variables, callable table, semantic nodes, and inline outgoing links.
- `macros/<macro>` — present only when the asset defines at least one macro.
- `properties/actor/tags` — zero-based pages of effective Actor tags.

The root advertises only non-empty namespaces. `CopyProperties` and `OverrideWith` are selector targets only when implemented by the Blueprint. Calls to inherited PlayerState functions appear through the selected graph's callable table.

## Implementation implications

- PlayerState descendants currently use the generic Actor family, so existing variable, graph, function, macro, component, replication, RPC, and snapshot collectors are reusable. Paging remains available only for non-graph collections.
- `asset_inspect` needs a dedicated PlayerState semantic classifier and adapter for framework defaults, fixed runtime contracts, and transfer-event identities.
- No companion is required.

## Open questions

- None at the current requirements layer. The property and transfer contracts are accepted, with actual runtime player values excluded.
