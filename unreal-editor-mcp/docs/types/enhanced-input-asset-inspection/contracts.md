# Enhanced Input inspection contracts

## Families and selectors

The admitted `unreal-mcp-enhanced-input` schema-revision-2 companion publishes inspection-only families `input_action`, `input_mapping_context`, `player_mappable_input_config`, `input_trigger_blueprint`, and `input_modifier_blueprint`. Each exposes one same-named record selector and composes the base Data Asset or ordinary Blueprint result. No family publishes creation or editing.

## Actions, mappings, and player settings

`input_action` reports exact value/accumulation enums, description, pause, consumption, reservation, legacy-consumption mask, effective player-mappable settings, and ordered trigger/modifier arrays. Each nested object has a deterministic identity, index, class, resolution and support kind, allowlisted persisted properties with type/export/source, and bounded unsupported custom-property names.

`input_mapping_context` reports description, input-mode filter/query, registration tracking, sorted profile IDs, and ordered default/profile mappings. A mapping record retains a duplicate-safe identity, profile and index, action reference, key name, player-mappable state, exact setting behavior, effective mapping/display/category values, effective settings, and ordered nested triggers/modifiers. Unresolved actions and nested objects remain explicit.

`player_mappable_input_config` reports config/display names, its own deprecation flag, metadata, and deterministically sorted context paths/priorities. It also returns `deprecated_in_ue_5_8: true` and names `UEnhancedInputUserSettings` as the replacement; runtime user-settings state is never inspected.

## Custom Blueprint records

Trigger and modifier Blueprint records identify the generated class and native base, declare composition with the ordinary Blueprint result, and list the fixed supported override points with availability and Blueprint implementation state. Base persisted Enhanced Input settings use the same allowlist. Custom Blueprint variables continue to appear through base typed-default inspection and are named in the overlay as custom defaults rather than being reflected recursively.

## Bounds, snapshots, and exclusions

Each family is limited to 512 mappings, 128 nested objects, 48 allowlisted persisted properties, 32 profile IDs or legacy contexts, 32 unsupported-property names, 4,096 encoded bytes per property, 65,536 value nodes, and four megabytes. Paging and graph flags on Enhanced Input selectors return `invalid_argument`; structural overflow returns `response_too_large`.

Snapshots include all bounded effective mappings, settings, nested class/property state, references, profile order, Blueprint overrides, and unsupported-data markers. Inspection does not load unresolved references, execute triggers/modifiers, inject input, query active devices or runtime mappings, mutate settings/assets, compile, save, or enable plugins.

[Types index](index.md) · [Architecture](../../architecture/enhanced-input-asset-inspection.md) · [User guide](../../user/enhanced-input-assets.md)
