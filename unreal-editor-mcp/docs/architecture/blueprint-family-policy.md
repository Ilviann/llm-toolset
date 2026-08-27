# Blueprint family policy

## Ownership

`UnrealMCPBlueprintFamilyPolicy` owns classification, operation, replication, replicated-variable, and RPC-mode policy shared by all Blueprint components and bridge capabilities.

## Dependency direction

The policy depends only on live Unreal classes, normal Blueprint state, local K2 graphs, and reflected Blueprint-event functions. The bridge consumes its published matrix. Inspector, mutator, action-catalog, and graph-editor components consume classification and operation checks; the policy does not depend on those components, transactions, package saving, HTTP, or MCP framing.

## Invariants

- `AGameMode` is classified before `AGameModeBase`, and `AGameState` before `AGameStateBase`; Blueprint-generated descendants retain the nearest published family.
- Every published family follows the existing inspection path. The seven authoring families retain the mutation path; inspection-only libraries and companion families reject it. No accepted operation bypasses path confinement, snapshot, identity, transaction, compile, save, action-filter, operation-ledger, or response-bound contracts.
- Capabilities publish a bounded nine-record base family/operation matrix. Discovery records and all exact-asset operation results report `blueprint_family`; exact inspection also reports live default, component, widget-tree, event-graph, local-variable, override, and graph-type capabilities.
- `UGameInstance` descendants classify as `game_instance`; `UUserWidget` descendants classify as `widget`; arbitrary UObject descendants remain unsupported. Both non-Actor families publish component support as false. Widget alone publishes widget-tree support, and unsupported component mutations reject before snapshot validation or transaction work. Actor-family component support is unchanged.
- `BPTYPE_FunctionLibrary` and `BPTYPE_MacroLibrary` assets classify as `function_library` and `macro_library` before parent inheritance is considered. Both publish only discovery and inspection, expose no replication or project-assignment surface, and reject every authoring operation. Class-based creation policy remains limited to the seven authoring families.
- Ready inspection-only companion families are appended by the extension registry instead of becoming base mutation families. Their selected Blueprint may expose ordinary read capabilities, while every base mutation operation stays false. `gameplay_ability` and `gameplay_effect` are therefore discoverable and inspectable only while the matching `UnrealMCPGAS` contributions are ready.
- Parent changes remain false. Project assignment is true only for GameModeBase, GameMode, and GameInstance; the separate settings editor still validates exact compatibility. Multiplayer records publish exact RPC modes and replication support per family.
- Live capability evaluation observes the selected Blueprint and does not imply that a particular graph, property, callback, or action exists without exact inspection or catalog resolution.

## Verification

Phase 14/15 cases cover the original family baselines. `UnrealMCP.Phase16.MultiplayerAuthoring` covers the published multiplayer matrix, typed defaults/components, three RPC modes, compile flags, persistence, and read-back. `UnrealMCP.WidgetTree` covers the widget family, specialized asset type, capability split, and persistence. `UnrealMCP.BlueprintLibraries.InspectionOnlyFamilies` covers both library families and the Actor-parent macro classification guard.
