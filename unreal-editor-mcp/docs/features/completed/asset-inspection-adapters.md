---
feature_id: asset-inspection-adapters
status: completed
depends_on:
  - asset-family-foundation
released_in: "0.40.0"
---

# `asset-inspection-adapters` — Asset inspection service decomposition

**Outcome:** `asset_inspect` orchestration is family-independent and existing core asset semantics live in focused inspection adapters.

**Depends on:**

- [`asset-family-foundation`](../completed/asset-family-foundation.md)

### Implementation

- Reduced `FUnrealMCPAssetInspectionService` to exact request decoding and target resolution, frozen registry selection, request orchestration, stable-snapshot and read-only checks, and final typed-document encoding.
- Registered separate `core_blueprint` and `neutral_asset` built-in descriptors before registry freeze. Blueprint storage classification delegates to focused Interface, standalone Actor Component, gameplay, and neutral Blueprint adapters; graph, collection, and semantic-property collaborators own their respective response behavior.
- Preserved the released request, deterministic YAML, selector, paging, graph, snapshot, limit, and error contracts exactly. The adapter context now retains whether paging and partial-graph fields were explicitly supplied so default values do not change validation behavior.
- Kept companion API v1 unchanged. A new built-in native class family can override the neutral fallback through one higher-priority descriptor and adapter without changing the coordinator or unrelated family selection; model-facing publication remains paired with the static Python catalog, fixtures, and documentation.

### Verification and completion evidence

- `UnrealMCP.AssetInspect.CoreFamiliesSelectorsPagingAndLimits` re-runs every released core family, media/neutral, selector, paging, graph-limit, UTF-8, snapshot, invalid-input, and non-mutation case through the registry-backed service.
- `UnrealMCP.AssetInspect.AdapterIsolation` proves an exact descriptor overrides only its neutral fallback while core Blueprint and unsupported-family selection remain unchanged.
- The complete Python suite covers the unchanged MCP schema, deterministic safe YAML, strict UTF-8 stdio, limits, errors, and catalog. UE 5.8 native Automation, lifecycle/restart acceptance, adaptive and true forced-unity Windows builds, and base Win64 packaging are the release gates for 0.40.0.
- macOS native follow-up passed on 2026-08-15 through the full Automation, production-socket restart, build-mode, and universal base-package gates; Linux remains outside the supported and verified scope.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
