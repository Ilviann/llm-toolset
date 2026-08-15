# Animation Blueprint inspection

## Ownership

`UnrealMCPAnimation` is a built-in leaf editor module. It owns the Animation Blueprint family classifier, semantic overlay, selector routes, cumulative animation fingerprint, and focused Automation tests. Its private dependency on `AnimGraph` and related editor modules keeps animation-editor types out of the host, Asset Core, Blueprint, UMG, and Content modules.

The common Blueprint module recognizes Animation Blueprint modes and supplies shared members, declarations, K2 semantics, and the exported read-only atomic graph seam. The animation module decorates that graph record with pose-oriented traversal and animation-node facts.

## Semantic model

- The root distinguishes `regular`, `template`, and `interface` modes and publishes only settings meaningful to the mode.
- Animation graphs carry stable names, `main_pose` or `animation_layer` kinds, signatures, node counts, and exact selectors.
- State-machine records contain their owning pose graph, entry state, states, conduits, aliases, transitions, rule selectors, and optional custom-blend selectors.
- Selected implementation graphs use the common graph/pin/link record. Partial traversal starts at the semantic output/result root and follows inputs toward pose producers.
- Interface selections stop at layer signatures; they do not claim an implementation graph.
- Parent overrides and property collections use deterministic zero-based pages and share the query-independent asset snapshot.

## Boundaries and invariants

- The overlay matches exact and derived `UAnimInstance` classes after core Blueprint classification; it does not add model-facing commands or schemas.
- All inspection is read-only and bounded. Snapshot input is independent of selector, verbosity, and page choices.
- Persisted semantic assets may be named, but animation payloads and referenced assets are never loaded or reconstructed.
- Compiler-generated properties, runtime proxies, skeletal components, debug poses, active states, montage playback, weights, thumbnails, and media are outside the contract.

## Implementation and verification

- Module and adapter: `plugin/UnrealMCP/Source/UnrealMCPAnimation/`.
- Shared graph seam: `plugin/UnrealMCP/Source/UnrealMCPBlueprint/Public/UnrealMCPBlueprintGraphInspection.h`.
- Focused native coverage: `UnrealMCPAutomationTestsAssetInspectAnimation.cpp`.
- Python ownership and packaging contracts: `tests/test_contracts.py` and `tests/test_asset_family_conformance.py`.

Run the Python suite, documentation linter, adaptive/forced-unity/non-unity UE 5.8 builds, native Automation, the headless production-socket lifecycle, and base Win64 packaging after changes.

[Architecture index](index.md) · [Feature](../features/completed/asset-inspect-animation.md) · [Type contract](../types/asset-inspection/asset-types/animation-blueprint.md)
