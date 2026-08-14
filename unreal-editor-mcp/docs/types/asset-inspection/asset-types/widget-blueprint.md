# Widget Blueprint

This family covers `UWidgetBlueprint` assets whose generated classes derive from `UUserWidget`. The `asset-inspect-umg` semantic view combines ordinary Blueprint logic with base UMG class defaults, effective widget hierarchy, layout slots, presentation, legacy property bindings, Designer event bindings, and named slots. Widget Animations, CommonUI-specific records, and MVVM-specific records remain excluded.

## Root response

```yaml
asset:
  type: widget_blueprint
  parent_type: /Script/UMG.UserWidget
widget_blueprint:
  palette_category: User Created
  can_initialize_without_player_context: false
  property_bindings_allowed: true
  tick:
    desired_frequency: auto
    compile_prediction: on_demand
    prediction_reason: animations_or_latent_actions
widget:
  visibility: visible
  enabled: true
  render_opacity: 1.0
  clipping: inherit
  volatile: false
  cursor: default
  render_transform: {translation: {x: 0.0, y: 0.0}, scale: {x: 1.0, y: 1.0}, shear: {x: 0.0, y: 0.0}, angle: 0.0}
  render_transform_pivot: {x: 0.5, y: 0.5}
  navigation: {up: {rule: escape}, down: {rule: escape}, left: {rule: escape}, right: {rule: escape}, next: {rule: escape}, previous: {rule: escape}}
user_widget:
  color_and_opacity: {r: 1.0, g: 1.0, b: 1.0, a: 1.0}
  foreground_color: inherited
  padding: {left: 0.0, top: 0.0, right: 0.0, bottom: 0.0}
  focusable: false
  input_action_priority: 0
  input_action_blocking: false
widget_tree:
  root_widget: RootCanvas
  widget_count: 42
  maximum_depth: 6
  widgets:
    kind: array
    count: 42
    selector: widget_tree
named_slots:
  kind: array
  count: 2
  selector: named_slots
bindings:
  property_count: 3
  event_count: 5
  selector: bindings
variables: []
event_graphs:
  - name: EventGraph
    events: [PreConstruct, Construct, Destruct, OnClicked_StartButton]
functions: [RefreshView]
macros: []
selectors: [widget_tree, widgets, named_slots, bindings, properties, event_graphs, events, functions]
```

## Root class-default rules

- `type` is `widget_blueprint`; `parent_type` is the exact immediate native or Widget Blueprint-generated parent class.
- `widget_blueprint` reports palette classification, initialization-without-player-context policy, effective legacy-binding availability, desired tick frequency, compile-time tick prediction, and its bounded reason.
- The cumulative `widget` block reports important `UWidget` defaults: visibility, enabled state, opacity, clipping, volatility, cursor, render transform/pivot, navigation, tooltip, and accessibility policy when authored. Navigation is flattened through a dedicated semantic adapter rather than exposed as an arbitrary instanced UObject graph.
- The cumulative `user_widget` block adds tint/foreground, padding, focusability, input-action priority/blocking, and other important supported `UUserWidget` defaults.
- Important properties from recognized derived base-UMG classes are added with exact declaring-type provenance. CommonUI and MVVM plugin properties are not generically reflected into the base response.
- Collection-valued class defaults use exact `properties/...` selectors and zero-based paging. Scalar and bounded struct values remain inline.

Do not return thumbnail settings/images, Designer preview size, preview DPI, editor zoom/pan, selected widgets, asset editor state, compiler layout records, generated widget templates, or debug instances.

## Widget-tree index

`widget_tree` returns a zero-based page of a deterministic parent-before-child widget index:

```yaml
selection:
  selector: widget_tree
widgets:
  - name: RootCanvas
    class: /Script/UMG.CanvasPanel
    ownership: local
    declared_by: /Game/UI/WBP_HUD.WBP_HUD_C
    root: true
    variable: false
    parent: null
    child_index: null
    child_count: 4
    selector: widgets/RootCanvas
  - name: HealthText
    class: /Script/UMG.TextBlock
    ownership: local
    declared_by: /Game/UI/WBP_HUD.WBP_HUD_C
    root: false
    variable: true
    parent: RootCanvas
    child_index: 0
    child_count: 0
    selector: widgets/HealthText
page:
  size: 2
  index: 0
  count: 21
  returned: 2
  total_items: 42
  has_next: true
  snapshot_id: 40_hex_snapshot
```

The effective index includes inherited and local widgets with `ownership` and `declared_by` provenance. Widget names are the normal semantic identities and selector keys; persistent Widget Blueprint GUIDs and hashed slot IDs remain internal unless a later debugging requirement justifies exposing them.

## Exact widget and layout selectors

`widgets/<widget-name>` returns one widget summary:

```yaml
widget:
  name: HealthText
  class: /Script/UMG.TextBlock
  ownership: local
  variable: true
  properties:
    count: 19
    selector: widgets/HealthText/properties
  slot:
    class: /Script/UMG.CanvasPanelSlot
    parent: RootCanvas
    child_index: 0
    properties:
      count: 4
      selector: widgets/HealthText/slot/properties
  children:
    kind: array
    count: 0
    selector: widgets/HealthText/children
  bindings:
    count: 1
    selector: widgets/HealthText/bindings
```

- `widgets/<name>/properties` pages all important cumulative properties from `UWidget` through recognized derived base-UMG widget classes. Records contain name, semantic type, value or collection descriptor, and exact declaring class.
- `widgets/<name>/slot/properties` pages all important layout properties from the exact panel-slot class hierarchy. Canvas slots include anchors, offsets, alignment, auto-size, and Z order; other recognized slots include padding, sizing/fill, alignment, row/column, spans, layer, nudge, wrapping, and ordering where supported.
- `widgets/<name>/children` pages child summaries in authored zero-based order.
- `widgets/<name>/bindings` pages bindings targeting the selected widget.
- A collection-valued widget or slot property uses an exact nested selector such as `widgets/Choice/properties/DefaultOptions`.

Every recognized widget subclass adds its important properties cumulatively. Examples include TextBlock text/font-reference/color/wrapping/justification; Image brush resource reference/draw mode/tint; Button interaction/style/focus; ProgressBar value/fill/marquee; Border brush/color/padding/alignment; CheckBox state/style; Slider and SpinBox ranges/steps; ComboBox options/selection/style; and editable-text content/input policy. Brushes and fonts expose semantic configuration and asset references, never texture pixels or font data.

Unknown base-UMG subclasses may add safe reflected properties declared by that subclass. Skip transient Slate resources, delegates, recursive instanced subobjects without a dedicated adapter, editor-only state, bulk data, and media payloads.

## Named-slot selectors

`named_slots` pages exact slot records containing host widget, slot name, current content widget, declaring Widget Blueprint, inherited/local ownership, availability to subclasses, and instance-exposure policy. Named-slot content relationships also appear in the widget index so the effective hierarchy remains understandable across pages.

## Binding selectors

`bindings` pages both supported base UMG binding kinds:

- Legacy property bindings report target widget/property, source property or pure/const zero-argument function, source/target types, declaring Blueprint, and `cost: polling`.
- Designer event bindings report target widget/delegate, bound graph/event identity, exact event selector, signature, declaring Blueprint, and `cost: event_driven`.

The root counts each kind separately. Binding source functions and event bodies remain ordinary atomic graph selectors. MVVM ViewModels, FieldNotify View Bindings, generated MVVM functions, and MVVM extension data are not included.

## Widget Animation exclusion

`asset-inspect-umg` does not inspect Widget Animation summaries, animation bindings, MovieScene tracks, sections, keyframes, or timeline configuration. Animation variables, callback events, and `PlayAnimation`-style calls may still appear where they are ordinary members or nodes in an inspected Blueprint graph; this does not imply inspection of the referenced timeline body.

## Blueprint logic selectors

- `event_graphs/<graph>` returns the complete semantic graph unless the caller explicitly enables the oversized partial-graph fallback.
- `events/<graph>/<event>` returns the complete reachable lifecycle, input, focus, animation callback, Designer event, or custom-event slice under the same fallback rule.
- `functions/<function>` and `macros/<macro>` use the shared signatures, locals, callable table, normalized nodes, inline links, verbose debug metadata, and atomic graph rules.

Ordinary UUserWidget callbacks such as Initialize, PreConstruct, Construct, Destruct, Tick, focus, input, drag/drop, paint, and animation events appear only when actually implemented and exposed by Unreal's live graph model. Do not synthesize unavailable override graphs.

## Excluded runtime state and plugins

Do not return a live widget instance, Slate widget, viewport presence, current geometry, desired size, focus/capture state, current visibility after runtime mutation, input routing state, active animations, playback positions, dynamic material instances, delegate invocation lists, or gameplay objects referenced only at runtime.

Base UMG inspection remains available for a Widget Blueprint derived from a plugin widget class when ordinary `UUserWidget` ancestry can be established. The base result reports exact opaque parent identity and supported base UMG blocks but omits CommonUI-specific and MVVM-specific semantics. No CommonUI or MVVM module dependency is added.

## Implementation implications

- The current family policy, Blueprint inspector, widget-tree inspector, layout/style services, binding service, stable snapshots, and graph/member collectors already provide most base logic, hierarchy, slot, changed-default, and binding data.
- `asset_inspect` needs a semantic projection with effective inherited/local hierarchy, cumulative important widget/slot properties, zero-based page-index routing, and collection-valued property selectors instead of the current edit-oriented IDs and changed-default truncation.
- Keep existing 512-widget, depth-32, 256-named-slot, and 256-binding safety ceilings initially. Paging changes response size, not the maximum asset complexity admitted for inspection.

## Open questions

- None at the current requirements layer. The accepted `asset-inspect-umg` scope is logic graphs, hierarchy, layout, styles, named slots, and ordinary base UMG bindings; Widget Animations are excluded.
