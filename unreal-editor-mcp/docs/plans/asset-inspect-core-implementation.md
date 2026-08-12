# Asset inspection core implementation plan

This working plan tracks implementation of the accepted `asset-inspect-core` contract. It is not an executable contract; the feature and type documents remain the approved requirements until completion.

## Confirmed requirements

- Publish one read-only `asset_inspect` tool for exact `/Game` assets with deterministic safe YAML success responses.
- Support neutral classification, gameplay-framework Blueprints, standalone Actor Component Blueprints, and Blueprint Interfaces.
- Use hierarchical UTF-8 percent-encoded selectors, zero-based collection pages, stable query-independent snapshots, direct `UEdGraph` traversal, atomic graphs, and explicit coherent partial-graph fallback.
- Remove `blueprint_inspect` from the published catalog, bridge dispatch, capabilities, tests, examples, and documentation once the core facade covers its core analysis role.
- Keep `game_data_inspect`; exclude Data Assets/Tables, UMG, Animation Blueprints, GAS, CommonUI, MVVM, Materials, Niagara, media payloads, runtime state, discovery, and reconstruction data.
- Do not change the companion API. Target only UE 5.8 and validate with `ue-test/ue58/UnrealMCPTest.uproject`.

## Discovered constraints

- The approved planned document originally conflicted with the user's later direct requirement by retaining `blueprint_inspect`; the direct requirement prevailed and the feature contract was corrected before implementation.
- Existing mutation, action-catalog, block-replacement, and widget services depend on the internal `FUnrealMCPBlueprintInspector` for structural snapshots and preconditions. Removing the external command does not permit deleting that internal component.
- Companion API v1 contributions still name the internal `blueprint_inspect` tool family. Changing that name or contribution protocol would require the separately approved companion-API workflow, so this feature leaves those native registration contracts unchanged and does not route them into `asset_inspect`.
- The native bridge response limit is 256 KiB. Asset graph completeness and partial slicing must be decided before the bridge serializes the JSON envelope; YAML rendering remains Python-owned.
- The repository now requires strict UTF-8 stdio streams and a subprocess regression under a forced non-UTF-8 inherited encoding.

## Implementation decisions

- Add a base-owned native asset inspection service with exact `/Game` resolution, classification, semantic projection, selector routing, paging, snapshots, and graph bounds.
- Reuse the internal Blueprint inspector's established codecs and direct graph semantics where practical, but produce a purpose-built compact JSON record instead of exposing mutation-oriented record pages.
- Keep `FUnrealMCPBlueprintInspector` as an internal collaborator while removing only its external route and public schema.
- Add a dependency-free deterministic safe-YAML renderer at the final MCP success boundary. Native and Python tests continue to assert JSON-shaped canonical records before rendering.
- Use response-local graph IDs derived from deterministic semantic order. Emit links once from source output pins and include native GUIDs/coordinates only in verbose debug blocks.

## Validation evidence

- The Markdown reader returned repository-relative workflow, index, feature, type, architecture, and testing sections on 2026-08-12.
- The selected UE 5.8 Engine plugin tree contained no installed repository-owned `UnrealMCP*` plugins before native work.
- Initial Git inspection showed no staged, unstaged, or untracked changes.
- Focused and full Python discovery passed: 160 tests, one expected skip.
- UE 5.8 adaptive and genuine forced-unity editor builds passed against `ue-test/ue58/UnrealMCPTest.uproject`. A true isolated non-unity UBT build with normal shared PCH support passed 92 separate source actions against the BuildPlugin HostProject.
- The full native Automation suite passed 46 cases after aligning the removed native-route assertion with the bridge's established `invalid_argument` code.
- The full headless lifecycle passed Blueprint and Widget authoring, asset references/deletion, level management/editing, Phase 17 game data, replay/restart, MCP framing, and graceful shutdown. Two post-transition calls were routed through existing bounded `editor_unavailable`-only readiness helpers discovered by repeated lifecycle validation.
- Isolated UE 5.8 Win64 base-plugin packaging passed.
- Optional `-StrictIncludes` packaging was also audited but is not a completion requirement: after the new asset service added its missing condensed-JSON policy include, the repository still fails PCH-free compilation in pre-existing asset-reference, level-service, and property-codec sources. This broader include-hygiene backlog is unrelated to `asset-inspect-core`; standard and non-unity/PCH-enabled builds pass.
- Final Python discovery passed 160 tests with one expected skip, and repository documentation lint passed 261 files across four documentation roots.
- The context-cache documentation audit found no repository cache/configuration to reconcile. The previously known external reconciliation helper is not installed at its recorded path, so no cache mutation was applicable.
- Final staged, unstaged, untracked, whitespace, version, and diff-scope checks passed. No files were staged.
