# Blueprint Interface

This family covers Blueprint assets whose Blueprint type is interface and whose generated class represents a `UInterface` contract. Its semantic content is the set of callable declarations that implementing Blueprints or native classes may fulfill. The interface asset contains no executable implementation graph.

## Root response

```yaml
asset:
  type: interface_blueprint
  parent_type: /Script/CoreUObject.Interface
interface:
  description: Interaction contract for world objects.
  function_count: 2
functions:
  - name: CanInteract
    dispatch: interface_message
    inputs:
      Interactor: object</Script/Engine.Actor>
    outputs:
      ReturnValue: bool
    traits: [pure, const]
    selector: functions/CanInteract
  - name: Interact
    dispatch: interface_message
    inputs:
      Interactor: object</Script/Engine.Actor>
    outputs: {}
    traits: []
    selector: functions/Interact
selectors: [functions]
```

## Identity and root properties

- Use the distinct stable type `interface_blueprint`; do not classify the asset as an Actor Blueprint or ordinary executable Blueprint merely because another class implements it.
- `parent_type` is the exact immediate represented interface superclass. For an ordinary Blueprint Interface this is `/Script/CoreUObject.Interface`; preserve a more specific native interface parent if Unreal permits one for the inspected asset.
- `description` is the user-authored Blueprint description when present. Omit it when empty.
- `function_count` and the compact `functions` index describe declarations owned by this interface. Each entry gives the ordered input/output signature, behavior-affecting traits, and exact detail selector.
- Do not emit empty `variables`, `event_graphs`, `macros`, `components`, or construction-script sections. Blueprint Interfaces cannot own state or executable implementations.

## Function declaration selector

`functions/<function>` returns the complete callable declaration, not a graph:

```yaml
selection:
  selector: functions/CanInteract
interface_function:
  name: CanInteract
  owner_type: /Game/Interaction/BPI_Interactable.BPI_Interactable_C
  dispatch: interface_message
  description: Whether this object currently permits interaction.
  category: Interaction
  traits:
    pure: true
    const: true
    deprecated: false
    call_in_editor: false
  inputs:
    - name: Interactor
      type: object</Script/Engine.Actor>
      passing: value
      default: null
  outputs:
    - name: ReturnValue
      type: bool
      passing: value
```

Preserve declared parameter order. For each parameter, expose its exact K2 type, direction, by-value/by-reference/const-reference policy, and declared default when meaningful. Include user-authored description, category, compact keywords, deprecation message, and other behavior- or usage-affecting callable metadata only when present. Omit editor colors, graph positions, zoom, node dimensions, and presentation-only metadata.

The selector has no `nodes`, `links`, local variables, or `graph_status`. It is non-graph data, so `allow_partial_graph` is invalid for it. Function declarations are individually selected rather than paged; any collection-valued reflected property discovered in a future interface-specific block still follows the shared property selector and paging rules.

## Interface call semantics

- `dispatch: interface_message` states that a caller invokes a contract on an object that may or may not implement it; it must not be represented as a direct call to an implementation owned by the interface asset.
- A declaration without outputs may appear as an event when an implementing Blueprint is inspected. A declaration with outputs normally appears as an implemented function. This difference belongs to the implementing asset; the interface response always reports the original declaration.
- Pure and const traits describe the declaration contract. Do not infer that every implementation is side-effect free beyond what Unreal enforces.
- Interface functions do not contain RPC, replication, latent execution, or default implementation bodies. Do not invent those properties from call sites or implementing assets.

## Relationships outside the asset

Do not search the project for implementers, callers, or interface-message nodes as part of `asset_inspect`. Those relationships are not encapsulated by the selected asset and can be numerous. Existing reference-analysis tools remain the appropriate path for project-wide relationships.

When another inspected Blueprint implements this interface, its root `implemented_interfaces` collection should reference this asset through the shared pageable property representation. Its function or event graph owns the executable body and is inspected through that Blueprint's normal graph selectors.

## Framework inheritance

- Include important callable declarations inherited from a meaningful native interface parent as well as declarations introduced by the Blueprint Interface, each once with declaring-type provenance.
- The compact root index must make inherited versus locally declared functions distinguishable without duplicating overridden declarations.
- `UInterface` object internals and generated-class bookkeeping are serialization or runtime infrastructure, not semantic asset properties, and are excluded.

## Implementation implications

- Add explicit classification for interface-type `UBlueprint` assets before ordinary Blueprint family routing.
- Reuse the common K2 type codec, function-signature normalization, safe metadata filtering, selector escaping, snapshot, and YAML rendering infrastructure.
- Read declarations from the interface's editor function graphs and generated function metadata, reconciling them by stable member identity. Treat entry/result nodes only as signature authoring records, not executable algorithm nodes.
- Do not load implementing classes or scan Asset Registry referencers while collecting this asset.

## Design status

The asset-family shape is accepted. The shared decision to list function input/output types in the root function index also applies to interface declarations.
