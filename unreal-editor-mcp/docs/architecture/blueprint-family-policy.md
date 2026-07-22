# Blueprint family policy

## Ownership

`UnrealMCPBlueprintFamilyPolicy` owns classification, operation, replication, replicated-variable, and RPC-mode policy shared by all Blueprint components and bridge capabilities.

## Dependency direction

The policy depends only on live Unreal classes, normal Blueprint state, local K2 graphs, and reflected Blueprint-event functions. The bridge consumes its published matrix. Inspector, mutator, action-catalog, and graph-editor components consume classification and operation checks; the policy does not depend on those components, transactions, package saving, HTTP, or MCP framing.

## Invariants

- `AGameMode` is classified before `AGameModeBase`, and `AGameState` before `AGameStateBase`; Blueprint-generated descendants retain the nearest published family.
- Every published family follows the existing inspection and authoring path. No family bypasses path confinement, snapshot, identity, transaction, compile, save, action-filter, operation-ledger, or response-bound contracts.
- Capabilities publish a bounded six-record family/operation matrix. Discovery records and all exact-asset operation results report `blueprint_family`; exact inspection also reports live default, component, event-graph, local-variable, override, and graph-type capabilities.
- `UGameInstance` descendants classify only as `game_instance`; arbitrary UObject descendants remain unsupported. GameInstance publishes and reports component support as false, and component mutations reject as `invalid_component` before snapshot validation or transaction work. Actor-family component support is unchanged.
- Parent changes remain false. Project assignment is true only for GameModeBase, GameMode, and GameInstance; the separate settings editor still validates exact compatibility. Multiplayer records publish exact RPC modes and replication support per family.
- Live capability evaluation observes the selected Blueprint and does not imply that a particular graph, property, callback, or action exists without exact inspection or catalog resolution.

## Verification

Phase 14/15 cases cover the family baselines. `UnrealMCP.Phase16.MultiplayerAuthoring` covers the published multiplayer matrix, typed defaults/components, three RPC modes, compile flags, persistence, and read-back.
