# Inspection expansion implementation

## Confirmed audit

- Existing: Data Asset/Primary Data Asset selectors, safe references, nested structured values, deterministic maps, Interface declarations, Animation Blueprint semantic graphs, GAS companion modifiers, and independent 4,096/262,144/262,144 inspection budgets.
- Gaps: reflected Gameplay Attribute values and K2 exports, field-local unsupported Data Table values, inherited effective component templates and provenance, library classification, and exact internal graph-name selection with mandatory scoped content.
- Public reads remain on `asset_inspect`; its exact hierarchical graph selectors already scope graph contents. The retired reconstruction inspector remains internal to snapshots, native tests, and companion collectors.

## Implementation decisions

- Extend Blueprint-owned reflection codecs and inspection adapters; preserve mutation admission and decoding, native companion API v2, and optional GAS dependencies.
- Inspect Gameplay Attributes using exact reflected struct identity and loaded property metadata, without linking GameplayAbilities or loading referenced assets.
- Keep whole-asset snapshot inputs independent of selected properties and graphs. Existing animation and modifier collectors remain authoritative.
- Implement reflected values first, then attributes, effective components, library classification, and scoped graph selection/callers.

## Verification evidence

- Initial working tree clean. UE 5.8 Engine plugin scan found no repository-owned plugin descriptors.
- Passed: 209 Python tests (one Windows symlink-permission skip), all 71 expected native Automation cases on the final binary, UE 5.8 Win64 adaptive/forced-unity/non-unity builds, isolated base packaging, the complete production-bridge restart workflow, and documentation lint. Expanded production-bridge selectors and library snapshots passed before and after restart. Fixture cleanup removes the two new disposable library assets on every run.
- Preferred macOS native qualification remains unperformed and is tracked in the roadmap. UE 5.7 three-field exports are tested as a data format on UE 5.8; no UE 5.7 native build is claimed.

## Completion

- Regression fixtures cover mixed supported/unsupported rows, Gameplay Attribute exports and maps, inherited component overrides, libraries, and graph selection rejection/paging.
- Capabilities, examples, Python/native version sources, and release history are synchronized to 0.60.0. The seven improvements are completed in dependency order; existing Data Asset, Gameplay Effect, animation, and independent budget support remains covered by the full suites. Companion API v2 and companion versions remain unchanged.
