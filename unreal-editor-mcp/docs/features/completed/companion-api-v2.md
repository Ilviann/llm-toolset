---
feature_id: companion-api-v2
status: completed
depends_on:
  - companion-plugins
  - native-domain-modules
  - python-asset-family-catalog
released_in: "0.44.0"
---

# `companion-api-v2` — Typed asset-family companion API

**Outcome:** One exact companion API v2 supports typed asset classification, inspection, creation, editing, read-back, persistence, and capabilities without exposing JSON or transport ownership to companions.

**Depends on:**

- [`companion-plugins`](companion-plugins.md)
- [`native-domain-modules`](native-domain-modules.md)
- [`python-asset-family-catalog`](python-asset-family-catalog.md)

### Implementation

- Advanced the base, `UnrealMCPGAS`, `UnrealMCPCommonUI`, and `UnrealMCPTestCompanion` descriptors and compiled constants atomically to exact `companion_api_version: 2` and extension schema revision 2. API v1, missing metadata, stale binaries, and mixed installations fail closed.
- Replaced every companion handler argument, result, change, read-back, and error-detail `FJsonObject` with bounded `FUnrealMCPRecord` and `FUnrealMCPValue` data. Removed Unreal's JSON module dependency from every shipped companion; only the base protocol and descriptor reader encode or decode JSON.
- Added `FUnrealMCPCompanionAssetFamily` as the complete bounded family seam for exact/derived classification, root and selector inspection, target-free creation, existing-target editing, stable nested identities, common snapshots, postcondition read-back, creation/edit persistence, typed capabilities, named limits, required modules, and domain adapters.
- Added atomic validation, deterministic sorting/signatures, collision checks, capability/adapter agreement, bounds, selector and nested-identity validation, and `asset_families` capability records. The Python catalog validates the bounded v2 record shape without accepting dynamic schemas.
- Preserved current GAS and CommonUI inspection behavior and kept their families outside `asset_inspect` until [`companion-asset-adapters`](companion-asset-adapters.md). This release publishes no new companion mutation.
- Released the base/Python package as 0.44.0, GAS as 0.2.2, CommonUI as 0.1.1, and the disposable fixture as 0.1.2.

### Verification

- Covered exact descriptor/compiled/base equality, v1 and schema mismatch rejection, missing or unavailable dependencies, duplicate and late registration, owner-checked shutdown, capability/adapter mismatch, family collisions, all three capability seams, selector routes, stable nested identities, deterministic capabilities, and unchanged base-only operation.
- Passed the full Python suite, adaptive/forced-unity/non-unity UE 5.8 editor builds, all native Automation cases, full production-socket headless integration, separate Win64 base/GAS/CommonUI/fixture packaging, documentation lint, and repository diff checks on Windows.
- macOS native follow-up passed on 2026-08-15 through full native and production-socket coverage plus isolated universal base, GAS, CommonUI, and fixture packages; Linux remains outside the supported scope.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
