# Standalone Actor Component Blueprint

This family covers a Blueprint whose generated class derives from `UActorComponent`, including `USceneComponent` descendants. It is separate from component instances or templates contained in an Actor Blueprint; those belong to the Actor asset's component hierarchy.

## Root response

```yaml
asset:
  type: actor_component_blueprint
  parent_type: /Script/Engine.ActorComponent
component:
  scene_component: false
  auto_activate: true
  editor_only: false
  tags:
    kind: array
    item_type: name
    count: 1
    selector: properties/component/tags
  replication:
    enabled: true
    requires_replicating_owner: true
    registered_subobject_list: false
  tick:
    can_ever_tick: true
    start_enabled: false
    interval_seconds: 0.1
    group: pre_physics
    dedicated_server: true
variables:
  - {name: Health, type: float, value: 100.0}
event_graphs:
  - name: EventGraph
    events: [BeginPlay, EndPlay, Tick]
functions:
  - name: ApplyDamage
    inputs: {Amount: float}
    outputs: {}
macros: [ClampHealth]
selectors: [event_graphs, events, functions, macros, properties]
```

## Root property rules

- `type` is `actor_component_blueprint`; `parent_type` is the exact immediate native or Blueprint-generated parent class path.
- `scene_component` reports whether the generated class derives from `USceneComponent`. Do not emit relative transform or attachment data for the standalone class asset; those are instance/template properties owned by an Actor Blueprint or level Actor.
- Activation reports the effective class-default `bAutoActivate` value.
- `editor_only` reports whether the component is excluded from non-editor builds.
- `tags` describes the effective component-tag collection because graph and gameplay logic may query it. Values are available through the returned zero-based pageable property selector.
- Replication reports the effective component replication flag and reminds the analyzer that component replication also requires a replicating owner Actor. Report registered-subobject-list mode because it materially changes replicated-subobject behavior.
- Tick configuration reports capability, initial enablement, interval, tick group, and dedicated-server policy. If ticking is impossible, emit `can_ever_tick: false` and omit inapplicable tick details.
- `variables` contains only locally declared custom Blueprint variables with K2 type and effective class-default value.
- Event graphs list only event nodes actually present in the asset. Functions and macros list only asset-owned declarations, using the shared deduplicated signature rules.
- Do not expose transient active/registered/initialized state, render dirtiness, editor visualization flags, asset-user-data payloads, or other instance/runtime state from the class default object.

## Framework inheritance

Inspection is cumulative across the supported component inheritance chain. A derived component returns all important `UActorComponent` properties above, all important properties contributed by intermediate supported Unreal framework types, and the important properties introduced by its most-derived supported type. Each property is emitted once in the semantic block owned by its declaring framework type.

Examples include:

- `USceneComponent` descendants may add class-default mobility and absolute location, rotation, and scale policy, but not an Actor-instance transform or attachment.
- `UPrimitiveComponent` descendants may add effective collision mode/profile, overlap-event policy, physics interaction, rendering or visibility behavior, and navigation relevance where those settings affect runtime semantics.
- More specialized component classes may add bounded references and configuration specific to their function, but never media payloads.
- A project or plugin subclass without a dedicated semantic adapter may expose bounded reflected properties declared by that subclass only when their types pass the safe property codec. Skip transient, editor-only, delegate, unsupported recursive object, and bulk or media fields explicitly.

Prefer one normalized block per recognized semantic class layer and include its exact declaring type. For example, a `UPrimitiveComponent` descendant contains the base `component` block, a scene-component block, and a primitive-component block. Do not dump the entire inherited Details panel; select properties that materially affect runtime semantics.

## Selectors

- `event_graphs/<graph>` — complete semantic graph using normalized nodes and inline outgoing links.
- `events/<graph>/<event>` — reachable event slice plus required pure/data dependencies.
- `functions/<function>` — signature, local variables, callable table, semantic nodes, and inline outgoing links.
- `macros/<macro>` — macro signature, callable table, semantic nodes, and inline outgoing links.
- `properties/component/tags` — zero-based pages of effective component tags.

The root `selectors` list advertises only namespaces with at least one available child. Empty event, function, or macro collections and their selector namespace are omitted.

## Implementation implications

- The current Blueprint family policy does not publish standalone `UActorComponent` descendants, so `asset-inspect-core` needs a new base family classification and capability record.
- Common Blueprint variable, callable, macro, event, graph, snapshot, and verbose-debug collectors should be reused through the semantic presentation layer. Paging infrastructure applies only to non-graph collections; every selected graph is returned completely.
- The component-specific root block can use `UActorComponent` and tick-function APIs already available through the base Engine dependency; it requires no companion.

## Open questions

- None at the current requirements layer.
