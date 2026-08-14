---
feature_id: enhanced-input-assets-inspect
status: planned
depends_on:
  - asset-inspect-core
  - companion-asset-adapters
released_in: null
---

# `enhanced-input-assets-inspect` — Enhanced Input asset inspection

**Outcome:** `asset_inspect` reports the static Enhanced Input configuration stored in Input Actions, Input Mapping Contexts, and supported custom trigger or modifier Blueprints without simulating input or reading runtime mappings.

**Depends on:**

- [`asset-inspect-core`](../completed/asset-inspect-core.md)
- [`companion-asset-adapters`](../completed/companion-asset-adapters.md)

### Asset-family scope

- Add an optional editor-only `UnrealMCPEnhancedInput` companion that owns direct `EnhancedInput` and required public editor-module dependencies. Reuse the released companion registry and `asset_inspect` contracts; add no Enhanced Input-specific model-facing tool and leave the base plugin fully usable when the Engine plugin or companion is unavailable.
- Inspect exact `UInputAction` assets with value type, accumulation and consumption policy, player-mappable settings, triggers, modifiers, stable nested identities, class paths, local values, and explicit unsupported custom data.
- Inspect exact `UInputMappingContext` assets with ordered action/key mappings, mapping identities, action references, keys, triggers, modifiers, player-mappable metadata, conflict-relevant flags, unresolved references, and one deterministic structural snapshot.
- Compose ordinary Blueprint inspection with typed defaults and supported events/functions for exact custom `UInputTrigger` and `UInputModifier` Blueprint assets. Report their use as inline instanced records inside actions and mappings without executing trigger or modifier logic.
- Provide read-only legacy inspection for existing `UPlayerMappableInputConfig` assets and mark the family deprecated in UE 5.8 output. Do not recommend it for new content or treat runtime `UEnhancedInputUserSettings` state as a project asset.
- Bound mappings, triggers, modifiers, referenced actions, nested values, class resolution, diagnostics, response size, and Game-thread time. Preserve unknown plugin subclasses as typed unsupported records rather than dropping them or using unrestricted reflection.

### Exclusions and completion gate

- Do not inject input, query active devices, evaluate triggers/modifiers, inspect per-player runtime mappings, mutate project input settings, add Blueprint event nodes, create or edit assets, or enable/install Enhanced Input.
- Verify all action value types, mapping order and repeated keys, built-in and custom triggers/modifiers, chained action references, player-mappable metadata, legacy configs, invalid and unresolved references, unavailable companion states, bounds, deterministic output, restart stability, and non-mutation.
- Complete only after focused and full Python tests, UE 5.8 public-header probes and native Automation, production-socket headless integration, adaptive/forced-unity/non-unity editor builds, and base/Enhanced Input Win64 packaging pass. Record remaining applicable macOS verification in the roadmap backlog.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
