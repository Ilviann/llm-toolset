---
feature_id: commonui-umg-types-inspect
status: planned
depends_on:
  - asset-inspect-umg
  - commonui-assets-inspect
  - companion-asset-adapters
released_in: null
---

# `commonui-umg-types-inspect` — CommonUI types in UMG inspection

**Outcome:** `asset_inspect` reports bounded CommonUI-specific widget and value semantics wherever supported CommonUI types occur in a Widget Blueprint, without requiring the Widget Blueprint root class to derive from `UCommonUserWidget`.

**Current coverage:** [`commonui-assets-inspect`](../completed/commonui-assets-inspect.md) is released. It recognizes `UCommonUserWidget`-derived Widget Blueprints, including `UCommonActivatableWidget` defaults, and contributes bounded input, activation, and reference records. This feature expands that completed root-class allowlist; it does not duplicate it.

**Depends on:**

- [`asset-inspect-umg`](../completed/asset-inspect-umg.md)
- [`commonui-assets-inspect`](../completed/commonui-assets-inspect.md)
- [`companion-asset-adapters`](../completed/companion-asset-adapters.md)

### Inspection contract

- Reuse the optional `UnrealMCPCommonUI` companion and the existing `asset_inspect` schema, safe-YAML result, selectors, paging, snapshot, limits, errors, and fail-closed companion admission. Add no CommonUI-specific model-facing tool and do not change companion API v2 unless a separately approved impact report proves that unavoidable.
- Admit a bounded CommonUI overlay for every otherwise supported Widget Blueprint while the companion and Engine plugin are ready. Preserve the released root-widget records and add semantic tree records for a frozen allowlist of CommonUI widgets, including button, text, rich-text, numeric/date text, action display, lazy image/widget, activatable container/switcher, tab/list, and carousel families supported by public UE 5.8 APIs.
- Decode the CommonUI-owned types stored on those widgets and root defaults, including style-class references, input-action and action-domain references, activation and navigation policy, transitions, selection state, input display/consumption policy, and container relationships. Report exact class paths, local versus inherited ownership, unresolved or unsupported values, and stable identities without loading soft references.
- Inspect a CommonUI child widget inside an ordinary `UUserWidget`-derived asset; do not require the asset's generated root class to derive from `UCommonUserWidget`. Keep base UMG records available when the companion is absent or rejected.
- Keep direct recursive inspection of referenced style/data assets, runtime activation and focus state, input-device simulation, dependency execution, project settings, mutation, compilation, saving, and plugin installation or enablement outside this feature.

### Verification and completion gate

- Cover ordinary Widget Blueprints with and without CommonUI children, released `UCommonUserWidget` and activatable roots, every frozen widget/value category, inherited values, unresolved references, selector paging, truncation, deterministic snapshots, and non-mutation.
- Prove fail-closed behavior for absent, disabled, unloaded, stale, or incompatible CommonUI dependencies and prove that base UMG inspection remains unchanged in every unavailable state.
- Complete only after focused and full Python tests, UE 5.8 native Automation, production-socket headless integration, adaptive/forced-unity/non-unity editor builds, and base/CommonUI Win64 packaging pass. Record remaining applicable macOS verification in the roadmap backlog.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
