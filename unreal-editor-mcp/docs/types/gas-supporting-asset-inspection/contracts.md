# Supporting GAS asset inspection contracts

## Families and selectors

The admitted companion publishes exact-and-derived inspection families `gameplay_cue_notify_static`, `gameplay_cue_notify_actor`, `attribute_set`, `gameplay_mod_magnitude_calculation`, and `gameplay_effect_execution_calculation`. The two disjoint Cue class roots both expose selector and root block `gameplay_cue_notify`; the other family IDs are also their selector and block names. All five compose ordinary Blueprint semantics and publish no creation or editing adapter.

## Cue records

`gameplay_cue_notify.summary` reports supported specialization, generated and parent classes, persisted Cue tag/name, and override policy. `event_responses` reports the four public `HandlesEvent` results. `persisted_settings.properties` contains exact name/type/exported-value/source records for the fixed base and Burst, Burst Latent, Looping, Hit Impact, cleanup, uniqueness, timing, placement, and effect allowlist. `asset_references` contains stable property-relative hard, class, and soft reference identities without loading unresolved targets.

The allowlist excludes transient spawn results, timer/recycle/runtime state, world actors, dispatched parameters, and arbitrary Blueprint-defined object layouts.

## Attribute and calculation records

`attribute_set.attributes` contains every supported `FGameplayAttributeData` or numeric property returned by the public class collector, sorted by property path. Each record includes a stable ID, name, property/owner identities, local or inherited source, value representation, base/current defaults, exact Net and RepNotify flags/function, and only explicitly persisted clamp/UI metadata.

Calculation `captures` preserve declared order and report stable duplicate-aware IDs, resolved attribute/property/class/asset identities, source or target capture, and snapshot policy. Magnitude `policy` reports non-authority dependency registration. Execution `policy` reports passed-tag policy, valid scoped-modifier captures, and sorted transient aggregator identifiers from public editor accessors. Unresolved native or Blueprint attribute references remain explicit records.

## Bounds and snapshots

Each family is limited to four top-level adapter records, 64 allowlisted persisted properties, 128 asset references, 256 attributes, 128 captures, 2,048 traversed nested values, 4,096 bytes per exported property, depth 8, the shared 65,536-value-node ceiling, and the shared four-megabyte document ceiling. Structural overflow returns `response_too_large`; paging and graph flags on the GAS selectors return `invalid_argument`.

The snapshot includes all bounded effective settings, references, attributes, captures, tags, policies, property flags, inheritance, and class identities even when the caller selects only one semantic block. Repeated root/selector inspection is deterministic and package state is preserved.

## Availability and exclusions

The companion must be ready on API v2/schema revision 2 with the exact seven-family Python catalog. `UAbilityTask` is not a standalone family. Runtime Ability System Components, granted specs/abilities, live attribute values, Gameplay Cue dispatch, calculation execution, prediction, replication state, C++ generation, asset discovery, mutation, compilation, and saving are excluded.

[Types index](index.md) · [Architecture](../../architecture/gas-supporting-asset-inspection.md) · [User guide](../../user/gas-supporting-assets.md)
