# Blueprint family policy

## Ownership

`UnrealMCPBlueprintFamilyPolicy` owns the explicit classification and operation policy shared by discovery, inspection, creation, existing-asset mutation, action cataloging, graph editing, and bridge capabilities. It classifies the native or generated live class into `actor`, `game_mode_base`, `game_mode`, `game_state_base`, `game_state`, or `game_instance`. Actor families use `actor_derived`; GameInstance uses `uobject_derived`.

## Dependency direction

The policy depends only on live Unreal classes, normal Blueprint state, local K2 graphs, and reflected Blueprint-event functions. The bridge consumes its published matrix. Inspector, mutator, action-catalog, and graph-editor components consume classification and operation checks; the policy does not depend on those components, transactions, package saving, HTTP, or MCP framing.

## Invariants

- `AGameMode` is classified before `AGameModeBase`, and `AGameState` before `AGameStateBase`; Blueprint-generated descendants retain the nearest published family.
- Every published family follows the existing inspection and authoring path. No family bypasses path confinement, snapshot, identity, transaction, compile, save, action-filter, operation-ledger, or response-bound contracts.
- Capabilities publish a bounded six-record family/operation matrix. Discovery records and all exact-asset operation results report `blueprint_family`; exact inspection also reports live default, component, event-graph, local-variable, override, and graph-type capabilities.
- `UGameInstance` descendants classify only as `game_instance`; arbitrary UObject descendants remain unsupported. GameInstance publishes and reports component support as false, and component mutations reject as `invalid_component` before snapshot validation or transaction work. Actor-family component support is unchanged.
- Parent changes and project-settings assignment remain explicitly false for every family. Unsupported classes fail family eligibility before inspection or mutation.
- Live capability evaluation observes the selected Blueprint and does not imply that a particular graph, property, callback, or action exists without exact inspection or catalog resolution.

## Verification

`UnrealMCP.Phase14.GameModeAndGameStateFamilies` covers the four Actor-based gameplay-framework lineages. `UnrealMCP.Phase15.GameInstanceFamily` covers UObject classification, live capabilities, explicit component rejection, a session-state default, function/local logic, Init callback cataloging and graph creation, compilation, saving, and read-back. The cross-process suite repeats representative authoring through the Python client and verifies every family after editor restart.
