# Gameplay Effect inspection contracts

## Family and request

The admitted family is `gameplay_effect`, its native operation is `inspect_gameplay_effect`, and its required live capability is `gas_gameplay_effects_inspection`. Callers still use `blueprint_inspect` with `mode: "discover"` or `mode: "inspect"`; exact inspection selects `gameplay_effect` in `sections`. The contribution is read-only and never publishes a model-facing GAS tool.

## Typed records

One successful typed inspection contributes exactly these section records:

- `gameplay_effect_duration` — duration policy and magnitude, maximum duration, period, periodic execution, and inhibition policy.
- `gameplay_effect_modifiers` — stable modifier IDs, operation, attribute reference, magnitude form, ownership, and duplicate state.
- `gameplay_effect_executions` — calculation classes, captures, conditional effects, scoped modifiers, and ownership.
- `gameplay_effect_stacking` — stacking type and limit, refresh/reset/expiration policies, overflow effects, and overflow flags.
- `gameplay_effect_cues` — cue tags, magnitudes, normalization flags, ownership, and stable cue IDs.
- `gameplay_effect_tags` — effective asset, granted, blocked-ability, and component-owned tag data.
- `gameplay_effect_granted_abilities` — ability class, level, input, removal policy, ownership, and stable grant ID.
- `gameplay_effect_additional_effects` — application/completion references, conditions, ownership, duplicate state, and stable reference ID.
- `gameplay_effect_requirements` — target, immunity, removal, custom-application, cancellation, and query requirements.
- `gameplay_effect_components` — component class, stable component ID, allowlisted type, ownership, support state, and owning data section.
- `gameplay_effect_relationships` — duration/period, stacking/overflow, ability, component, and chained-effect consistency findings.

On UE 5.7, an execution-conditional effect reference contains its effect class and required source tags. Later-engine removal-policy and stack-removal fields do not exist on `FConditionalGameplayEffect` and are therefore not published or inferred on this branch.

Magnitude records support only `scalable_float`, `attribute_based`, `custom_calculation_class`, and `set_by_caller`. Modifier operations are returned as their stable Unreal enum names. Unknown magnitude layouts or component classes remain explicit with `supported: false`; unresolved references keep empty paths plus `resolved: false` and compatibility state.

## Identity, inheritance, and snapshots

Nested IDs are deterministic lowercase hashes of semantic owner, class/reference, and ordinal inputs. Records report `source: "local"` or `source: "inherited"`; identical ancestor array entries and component data retain inherited ownership. Duplicate semantic identities are flagged rather than collapsed.

The companion fingerprint includes the full bounded Gameplay Effect configuration, effective tags, and chained-effect edges. The base combines it with ordinary Blueprint state into the page `snapshot_id`, so continuation rejects stale data even when the changed companion record was outside the visible page.

## Limits and exclusions

Runtime capability records publish the authoritative limits. Release defaults are 11 section records, 128 modifiers, 64 executions, 128 cues, 64 components, 128 granted abilities, 256 effect references, 128 requirements, 64 relationships, 2,048 scanned collection entries, chain depth 8, and 128 chained assets. Exceeding a scan or output bound returns `response_too_large`; chained depth/asset truncation is also reported in relationships.

Creation, compilation, saving, default/member/component/graph mutation, action discovery, runtime specs or application, Ability System Component mutation, Attribute Set or Gameplay Cue authoring, supplied calculations, arbitrary component authoring, and C++ generation are excluded.

[Types index](index.md) · [Architecture](../../architecture/gas-gameplay-effect-inspection.md) · [User guide](../../user/gameplay-effects.md)
