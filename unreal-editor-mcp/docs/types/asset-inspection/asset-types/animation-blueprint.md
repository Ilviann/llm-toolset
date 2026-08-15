# Animation Blueprint

This family covers `UAnimBlueprint` assets and their generated `UAnimInstance` classes. It must expose both ordinary Blueprint logic and the animation-specific pose/state-machine algorithms without returning animation media or live debug poses.

## Root response

```yaml
asset:
  type: animation_blueprint
  parent_type: /Script/Engine.AnimInstance
animation_blueprint:
  mode: regular
  target_skeleton: /Game/Characters/SK_Mannequin_Skeleton.SK_Mannequin_Skeleton
  root_motion_mode: root_motion_from_montages_only
  threaded_update:
    requested: true
    project_setting_required: true
    warn_about_blueprint_usage: true
  linked_layers:
    share_instances: false
    receive_notifies: false
    propagate_notifies: false
    use_main_instance_montage_data: false
  compiled_features:
    uses_copy_pose_from_mesh: false
  sync_groups:
    kind: array
    item_type: name
    count: 1
    selector: properties/animation_blueprint/sync_groups
  implemented_interfaces:
    kind: array
    item_type: asset_reference
    count: 1
    selector: properties/animation_blueprint/implemented_interfaces
  parent_asset_overrides:
    count: 2
    selector: parent_asset_overrides
variables:
  - {name: Speed, type: float, value: 0.0}
event_graphs:
  - name: EventGraph
    events: [InitializeAnimation, UpdateAnimation, ThreadSafeUpdateAnimation]
animation_graphs:
  - name: AnimGraph
    kind: main_pose
    selector: animation_graphs/AnimGraph
  - name: UpperBody
    kind: animation_layer
    selector: animation_graphs/UpperBody
state_machines:
  - name: Locomotion
    owner_graph: AnimGraph
    states: 5
    transitions: 8
    selector: state_machines/AnimGraph/Locomotion
functions:
  - name: CalculateLean
    kind: function
    inputs: {DeltaSeconds: float}
    outputs: {Lean: float}
macros: []
selectors: [event_graphs, events, animation_graphs, state_machines, functions, parent_asset_overrides, properties]
```

## Asset modes and identity

- A normal skeleton-bound asset uses `type: animation_blueprint` and `mode: regular`.
- A skeleton-independent template uses `type: animation_blueprint_template`, `mode: template`, and omits `target_skeleton`.
- An Animation Layer Interface uses `type: animation_blueprint_interface`, `mode: interface`, and exposes layer signatures rather than executable state/pose bodies.
- `parent_type` is the exact immediate native or Animation Blueprint-generated parent class when class inheritance is meaningful.

The separate type values prevent a caller from assuming that a template has a target skeleton or that an interface contains executable pose logic.

## Root property rules

- `target_skeleton` is an asset reference used for compatibility; no skeleton hierarchy, mesh, animation frames, curves, thumbnails, or other media payload is returned by default.
- `root_motion_mode` is the effective `UAnimInstance` class default.
- `threaded_update` reports the asset's multithreaded-update request and compiler-warning policy. It explicitly notes that actual worker-thread execution also depends on external project settings.
- `linked_layers` reports instance-sharing, notify propagation, and montage-data policies inherited from `UAnimBlueprint` and `UAnimInstance`.
- `compiled_features` contains small semantic facts derived by the compiler, such as use of Copy Pose From Mesh. Do not dump compiler-generated node layouts, sparse data, indices, or baked bytecode.
- `sync_groups` describes the group-name collection; values use its zero-based pageable property selector and editor-only colors are excluded.
- `implemented_interfaces` describes the exact Animation Layer Interface reference collection and exposes its values through a zero-based pageable property selector.
- `parent_asset_overrides` gives a count and selector. Its detail resolves each overridden parent node to a semantic graph/node identity where possible and returns the replacement animation-asset path, not the parent node GUID in normal mode.
- Variables, event graphs, functions, and macros follow the shared Blueprint rules. Normalize lifecycle callback identities to editor-facing names while retaining native identities internally.

Preview meshes, preview Animation Blueprints, preview application methods, pose watches, graph colors, debug objects, and compilation-layout records are editor/debug state and are excluded from the normal semantic response.

## Animation graph selectors

`animation_graphs/<graph>` returns all semantic animation nodes and pose/data links in that graph. It follows the shared short response-local node IDs, inline outgoing links, callable deduplication, and `verbose` GUID/coordinate rules.

Important node records include only behavior-affecting fields. Examples include referenced animation assets, looping and play-rate policy, blend parameters, sync group and role, slot name, cache-pose identity, per-bone blend filters, skeletal-control settings, linked graph/layer class and tag, exposed pin literals, and state-machine selector references. They never contain animation frames, pose samples, raw curves, thumbnails, or mesh data.

Animation Layer graphs use the same selector and add their input/output pose and value signature in the selected graph header. Interface-declared but unimplemented layers expose signatures without node bodies.

## State-machine selectors

`state_machines/<owner-graph>/<machine>` returns a semantic topology index:

```yaml
selection:
  selector: state_machines/AnimGraph/Locomotion
state_machine:
  name: Locomotion
  entry_state: Idle
  states:
    - name: Idle
      kind: state
      always_reset_on_entry: false
      selector: states/AnimGraph/Locomotion/Idle
    - name: LocomotionAlias
      kind: alias
      selector: states/AnimGraph/Locomotion/LocomotionAlias
  transitions:
    - key: Idle_to_Run
      from: Idle
      to: Run
      priority: 1
      disabled: false
      bidirectional: false
      blend: {duration_seconds: 0.2, mode: cubic}
      rule_selector: transitions/AnimGraph/Locomotion/Idle_to_Run
```

The state-machine index also represents conduits, aliases, state entry/exit/fully-blended notifications, automatic remaining-time rules, re-entry delay, required marker sync group, inertialization policy, shared rules/crossfades, transition notifications, custom blend presence, and active-only evaluation where applicable. Duplicate display names receive deterministic selector keys exposed in the parent index.

- `states/<owner-graph>/<machine>/<state-key>` returns the state's complete pose graph. Conduits return their rule graph; aliases return their bounded target-state policy rather than a fabricated pose graph.
- `transitions/<owner-graph>/<machine>/<transition-key>` returns the complete Boolean transition-rule graph plus the transition configuration header.
- `transition_blends/<owner-graph>/<machine>/<transition-key>` exists only for a custom transition blend and returns its pose graph.

The root lists state machines with counts and exact selectors rather than listing every state and transition. Selecting a state machine reveals the next level of exact child selectors.

## Excluded runtime state

Do not return the current skeletal mesh component, owning Actor, active state, state weights, transition progress, montage instances, current pose, curves, notify queue, linked instance objects, cached poses, proxy memory, update counters, or debug traces. These are live-instance observations, not data encapsulated by the asset.

## Framework inheritance

- The response includes important `UAnimBlueprint` asset properties and important `UAnimInstance` class defaults and contracts.
- A derived Animation Blueprint retains inherited animation settings, graphs or asset overrides as applicable and adds its own important properties and graphs with declaring-type or owner-asset provenance.
- Unknown native or plugin `UAnimInstance` subclasses may add bounded safe class-default properties. Skip transient runtime references, delegates, recursive objects, editor-only state, bulk data, and media payloads.

## Implementation implications

- Animation Blueprints are a published built-in inspection family. `UnrealMCPAnimation` owns the `AnimGraph`-dependent overlay for pose graphs, state machines, states, conduits, aliases, transitions, layers, and parent asset overrides while the common Blueprint adapter owns shared members and K2 semantics.
- Reuse the common Blueprint variable, event, function, macro, graph, node, link, property-codec, snapshot, and verbose-debug infrastructure. Reuse paging only for non-graph indexes such as parent asset overrides; pose, state-machine, state, conduit, transition-rule, and transition-blend graphs remain atomic.
- Treat editor graphs as the semantic source. Compiled baked state-machine and anim-node records may validate or supplement stable facts but must not replace source graph traversal or leak compiler layout details.
- The base plugin can implement this through Unreal Engine and AnimGraph dependencies; no domain companion is required.

## Open questions

- None at the current requirements layer. Regular, template, and Animation Layer Interface assets use distinct `asset.type` values.
