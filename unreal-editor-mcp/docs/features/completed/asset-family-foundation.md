---
feature_id: asset-family-foundation
status: completed
depends_on:
  - asset-inspect-core
  - native-command-catalog
released_in: "0.39.0"
---

# `asset-family-foundation` — Built-in asset-family adapter contracts

**Outcome:** Built-in asset types can register through deterministic family descriptors with separate inspection, creation, and editing capabilities.

**Depends on:**

- [`asset-inspect-core`](asset-inspect-core.md)
- [`native-command-catalog`](native-command-catalog.md)

### Implementation

- Added bounded stable family IDs, exact or derived class policy, priority, required modules, common and named limits, and independent inspection, creation, and editing declarations.
- Added typed inspection, creation, and edit adapter interfaces with resolved contexts, semantic value records, bounded document and snapshot builders, and longest-prefix selector routing.
- Added a base-owned registry that validates capability/adapter agreement, collisions, class ambiguity, dependency availability, and deterministic ordering/fingerprints, then freezes before bridge construction. Command composition rejects a mutable registry.
- Kept target resolution, access policy, MCP schemas, mutation replay, transaction/persistence/rollback authority, and response encoding outside adapters. Existing families retain their released service paths until their dependent migration features.
- Kept companion API v1 unchanged; this trusted built-in foundation does not expose optional companion adapter registration.

### Verification and completion evidence

- `UnrealMCP.AssetFamilies.RegistrySelectionCapabilitiesAndFreeze` covers exact and derived selection, priorities, ambiguity, unavailable modules, collisions, late registration, independent capabilities, capability disagreement, stable ordering, and restart-deterministic fingerprints.
- `UnrealMCP.AssetFamilies.BoundedBuildersAndSyntheticAdapter` proves a synthetic family inspection adapter can contribute typed semantic values, selectors, and snapshot material through the bounded contracts without bridge changes.
- UE 5.8 public-boundary probes plus adaptive and true forced-unity Windows editor-target builds compile the descriptor and all three adapter contexts.
- The Python suite, all native Automation, headless integration, and base Win64 packaging are the release gates recorded with version 0.39.0. macOS native verification remains preferred follow-up work.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
