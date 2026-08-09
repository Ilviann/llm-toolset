# PlayerController Blueprint

This family covers Blueprints whose generated classes derive from `APlayerController`. The response cumulatively describes the important `AActor`, `AController`, and `APlayerController` defaults and runtime contracts.

## Root response

```yaml
asset:
  type: player_controller_blueprint
  parent_type: /Script/Engine.PlayerController
actor:
  world_presence: server_and_owning_client
  placeable: false
  tags:
    kind: array
    item_type: name
    count: 0
    selector: properties/actor/tags
  replication:
    only_relevant_to_owner: true
    priority: 3.0
  tick:
    can_ever_tick: true
    start_enabled: true
    group: pre_physics
    even_when_paused: true
controller:
  attach_to_pawn: false
  possession_authority: server
  seamless_travel_eligibility: when_player_state_exists
  runtime_contract:
    pawn: replicated_from_authority
    player_state: replicated_from_authority
    control_rotation: controller_view_rotation
player_controller:
  camera:
    manager_class: /Script/Engine.PlayerCameraManager
    auto_manage_target: true
    remote_view_rotation_smoothing_speed: 20.0
  input:
    override_player_input_class: null
    motion_controls: true
    full_tick_when_paused: false
  pointer:
    show_mouse_cursor: false
    click_events: false
    touch_events: true
    mouse_over_events: false
    touch_over_events: false
    click_keys:
      kind: array
      item_type: key
      count: 1
      selector: properties/player_controller/click_keys
    click_trace_channel: visibility
    trace_distance: 100000.0
  feedback:
    force_feedback_enabled: true
    force_feedback_scale: 1.0
  cheat_manager_class: /Script/Engine.CheatManager
  world_partition_streaming:
    enabled: true
    target_state: activated
    block_on_slow_streaming: true
    priority: default
    shapes:
      count: 12
      selector: world_partition_streaming/shapes
  runtime_contract:
    player: local_player_or_network_connection
    hud: owning_player
    camera_manager: owning_player
variables:
  - name: SelectedUnit
    type: object</Script/Engine.Actor>
    value: null
    replication: none
event_graphs:
  - name: EventGraph
    events: [BeginPlay, OnPossess, OnUnPossess]
functions:
  - name: SelectUnit
    kind: function
    inputs: {Unit: object</Script/Engine.Actor>}
    outputs: {Succeeded: bool}
macros: []
selectors: [event_graphs, events, functions, properties, world_partition_streaming]
```

## Root property rules

- `parent_type` is the exact immediate native or Blueprint-generated parent. `type` is `player_controller_blueprint` for all `APlayerController` descendants.
- The inherited `actor` block reports the owning-player network scope, non-placeability, effective tags, owner relevancy, network priority, and tick defaults. Exclude transforms, rendering, collision, damage, and other Actor fields hidden or semantically irrelevant for controllers.
- The `controller` block reports whether its transform component follows the possessed Pawn, server authority over possession, conditional seamless-travel participation, and the runtime roles of the possessed Pawn, PlayerState, and control rotation.
- `player_controller.camera` reports the effective camera-manager class, automatic view-target policy, and remote target-view smoothing speed. Do not serialize a live camera manager or view target.
- `input` reports the optional PlayerInput class override, motion-control policy, and whether a full rather than minimal controller tick runs while paused. A null override means the effective class comes from external Input Settings; do not resolve it as asset-owned data.
- `pointer` reports effective cursor, click, touch, hover, trace-channel, and trace-distance defaults because they materially affect interaction logic. Click keys use a zero-based pageable property selector.
- `feedback` reports effective force-feedback enablement and scale.
- `cheat_manager_class` is the configured class default, not a claim that cheats or a CheatManager instance are available in the current build or session.
- `world_partition_streaming` remains in the `asset-inspect-core` root response, but only as a compact summary: whether the controller is a streaming source, whether loaded cells activate, slow-streaming blocking, priority, shape count, and the exact detail selector. Exclude the debug color.
- Blueprint variables include locally declared defaults and replication/RPC semantics under the shared Actor rules. Event graphs, functions, macros, and components use the shared Blueprint and Actor-owned component rules.

## Excluded runtime state

Do not return the current connection or local player, acknowledged or possessed Pawn objects, PlayerState instance, HUD, camera manager, current view target or rotations, input stack, active feedback effects, current cursor or clickable primitives, streaming runtime state, seamless-travel counters, muted players, spectator state, or other live connection/session data.

Deprecated legacy input yaw, pitch, and roll scales are omitted from the normal response. They may appear only in a future compatibility or verbose-semantic filter if analysis of legacy input behavior requires them; `verbose` alone remains graph-debug metadata and does not enable deprecated property dumps.

## Framework inheritance

- The response includes important Actor properties, Controller properties and contracts, and PlayerController-specific configuration.
- Supported engine, project, or plugin subclasses add important semantic properties without removing inherited blocks. Emit each property once with exact declaring-type provenance.
- Unknown subclasses may add bounded safe reflected class defaults declared by the subclass. Skip transient state, delegates, recursive object graphs, editor-only data, and media-bearing fields.

## Selectors

- `event_graphs/<graph>` — complete semantic graph.
- `events/<graph>/<event>` — selected lifecycle, possession, input, replication, RPC, or custom-event execution slice plus required data dependencies.
- `functions/<function>` — signature, function or override kind, declaring class, local variables, callable table, semantic nodes, and inline outgoing links.
- `macros/<macro>` — present only when the asset defines at least one macro.
- `properties/actor/tags` — zero-based pages of effective Actor tags.
- `properties/player_controller/click_keys` — zero-based pages of click-event keys in authored order.
- `world_partition_streaming/shapes` — bounded, pageable semantic definitions of custom streaming-source shapes. Omit this selector when there are no custom shapes.

The root advertises only non-empty namespaces. Calls to inherited controller, camera, input, travel, networking, and UI functions appear in selected graphs' callable tables rather than as selector targets unless the asset owns an override graph.

## Implementation implications

- PlayerController descendants currently use the general Actor family, so the graph, variable, replication, RPC, component, and callable collectors are reusable.
- `asset_inspect` needs a dedicated semantic classifier and cumulative framework adapter to distinguish PlayerController from a generic Actor response.
- All base properties are available from Engine reflection or stable class semantics; no companion dependency is required. World Partition source shapes require explicit output bounds.

## Open questions

- None at the current requirements layer. World Partition streaming remains supported, with scalar policy in the root and potentially large source-shape definitions behind a selector.
