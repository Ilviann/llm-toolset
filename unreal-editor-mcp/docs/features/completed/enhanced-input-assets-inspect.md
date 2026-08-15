---
feature_id: enhanced-input-assets-inspect
status: completed
depends_on:
  - asset-inspect-core
  - companion-asset-adapters
released_in: "0.52.0"
---

# `enhanced-input-assets-inspect` — Enhanced Input asset inspection

**Outcome:** `asset_inspect` reports static Enhanced Input configuration stored in Input Actions, Input Mapping Contexts, legacy player-mappable configs, and custom trigger or modifier Blueprints without simulating input or reading runtime mappings.

**Implementation status:** Completed in 0.52.0 with the independent `UnrealMCPEnhancedInput` 0.1.0 companion on unchanged companion API v2 and schema revision 2. Five read-only families compose with the base Data Asset or Blueprint result, while an absent or rejected companion leaves the base contract unchanged.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)
- [`companion-asset-adapters`](companion-asset-adapters.md)

### Released inspection contract

- `input_action` reports value type, accumulation and consumption policy, pause/reservation settings, player-mappable settings, and ordered inline trigger/modifier records.
- `input_mapping_context` reports description, input-mode and registration policy, sorted override-profile identities, and every ordered default/profile action-key mapping with stable mapping identity, action/key reference, effective player-mappable metadata, triggers, and modifiers.
- `player_mappable_input_config` provides bounded read-only legacy config/context inspection and always marks the family deprecated in UE 5.8 in favor of `UEnhancedInputUserSettings`.
- `input_trigger_blueprint` and `input_modifier_blueprint` compose ordinary Blueprint defaults, members, functions, events, and graphs with their Enhanced Input native base, supported override points, allowlisted persisted settings, and explicit unsupported custom defaults. Inline unknown plugin subclasses remain typed unsupported records rather than being dropped.
- All families use exact shared paths, one exact semantic selector, deterministic safe YAML, cumulative snapshots, stable nested identities, non-mutation checks, and fixed companion availability contracts.

### Bounds and exclusions

- Per-family bounds are 512 mappings, 128 triggers/modifiers, 48 allowlisted persisted properties, 32 profiles, 32 explicitly reported unsupported properties, 4,096 bytes per exported property, the shared 65,536 value-node ceiling, and the shared four-megabyte document ceiling. Overflow fails closed.
- Input injection, active-device queries, trigger/modifier evaluation, runtime player mappings and user settings, project input settings, asset creation/editing, compilation, saving, and plugin enablement remain excluded.

### Verification

UE 5.8 public headers establish every asset, mapping, settings, trigger, and modifier boundary. Native Automation covers all five families, built-in nested objects, custom Blueprints, deterministic fingerprints, non-mutation, and persistent fixtures. Python tests cover exact family admission, stable shared schemas, packaging selection, and release metadata. Windows adaptive, forced-unity, non-unity, production-socket restart, full Automation, and isolated base/companion packaging gates complete the release; macOS verification remains preferred follow-up work.

[Back to roadmap](../../../ROADMAP.md) · [Wire contracts](../../types/enhanced-input-asset-inspection/index.md) · [User guide](../../user/enhanced-input-assets.md)
