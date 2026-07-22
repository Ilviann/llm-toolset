# Blueprint family policy

## Ownership

`UnrealMCPBlueprintFamilyPolicy` owns the explicit classification and operation policy shared by discovery, inspection, creation, existing-asset mutation, action cataloging, graph editing, and bridge capabilities. It classifies the native or generated live class into `actor`, `game_mode_base`, `game_mode`, `game_state_base`, or `game_state`, while retaining `actor_derived` as the single inheritance category.

## Dependency direction

The policy depends only on live Unreal classes, normal Blueprint state, local K2 graphs, and reflected Blueprint-event functions. The bridge consumes its published matrix. Inspector, mutator, action-catalog, and graph-editor components consume classification and operation checks; the policy does not depend on those components, transactions, package saving, HTTP, or MCP framing.

## Invariants

- `AGameMode` is classified before `AGameModeBase`, and `AGameState` before `AGameStateBase`; Blueprint-generated descendants retain the nearest published family.
- Every published family follows the existing Actor-derived inspection and authoring path. No family bypasses path confinement, snapshot, identity, transaction, compile, save, action-filter, operation-ledger, or response-bound contracts.
- Capabilities publish a bounded five-record family/operation matrix. Discovery records and all exact-asset operation results report `blueprint_family`; exact inspection also reports live default, component, event-graph, local-variable, override, and graph-type capabilities.
- Parent changes and project-settings assignment remain explicitly false for every family. Unsupported classes fail family eligibility before inspection or mutation.
- Live capability evaluation observes the selected Blueprint and does not imply that a particular graph, property, callback, or action exists without exact inspection or catalog resolution.

## Verification

`UnrealMCP.Phase14.GameModeAndGameStateFamilies` covers all four gameplay-framework lineages through creation, classification, live capability inspection, family defaults, components, functions, locals, framework callbacks and inherited actions, compilation, and saving. The cross-process suite repeats representative authoring through the Python client and verifies every family after editor restart.
