# Gameplay frameworks and data

## GameMode and GameState families

The four gameplay-framework families reuse the complete Actor-derived path: creation, targeted defaults, local SCS components, variables, function/local shells, macros, custom events, live action discovery, graph editing, compile diagnostics, saving, operation reconciliation, and restart read-back. Check `capabilities.blueprint_families` before authoring. `parent_change` remains false for every family; `project_settings_assignment` is true only for `game_mode_base`, `game_mode`, and `game_instance`.

Useful GameMode defaults include `GameStateClass`, `PlayerControllerClass`, `DefaultPawnClass`, `bUseSeamlessTravel`, and, for `AGameMode`, `bDelayedStart` and `MinRespawnDelay`. GameState families support safe inherited Actor defaults and `ServerWorldTimeSecondsUpdateFrequency`. Property support remains a live reflected decision, so inspect a targeted `class_defaults` property before writing it.

Use the action catalog for inherited framework behavior instead of guessing an override node. Representative actions include GameMode login/match callbacks and callable functions such as `GetDefaultPawnClassForController` or `GetMatchState`; GameState families expose inherited Actor events and state/time functions such as `GetServerWorldTimeSeconds`, `HasBegunPlay`, `HasMatchStarted`, and `HasMatchEnded`. Unreal's live graph filter decides what is valid in the selected graph.

Local components use the same ownership rules as Actor Blueprints: local SCS components are editable; inherited and native components are read-only. The bridge can assign a compatible, clean, saved GameMode class as the project's default through `gameplay_framework_edit`; GameState classes and world-specific GameMode overrides remain outside that command. See [`examples/game-mode-game-state-workflow.json`](../../examples/game-mode-game-state-workflow.json) for focused requests.

## GameInstance family

GameInstance uses the shared creation, inspection, default/member editing, action-catalog, graph-editing, compile, save, diagnostics, operation-reconciliation, and restart-read-back contracts. Its published inheritance category is `uobject_derived`. Live capabilities normally report class defaults, event graphs, local variables, overrides, and event/function/macro graph types as available, but `components` is always false. `blueprint_component_edit` returns `invalid_component` before opening a transaction or changing the snapshot.

A practical GameInstance default is a user-defined instance-editable variable such as a session region, profile slot, or matchmaking preference. Add the member, compile so the generated-class property exists, then use `blueprint_default_edit` with the newest snapshot. Inspect the exact property through `class_defaults` to verify it.

Use `blueprint_action_catalog` to discover callbacks exposed by `UGameInstance`, including `ReceiveInit` (`Init`), `ReceiveShutdown` (`Shutdown`), `HandleNetworkError`, and `HandleTravelError`. Add the returned action through `blueprint_graph_edit`; the live catalog suppresses a unique callback once it exists. Assign a compatible, clean, saved class through `gameplay_framework_edit` when it should become the active project's default GameInstance. See [`examples/game-instance-workflow.json`](../../examples/game-instance-workflow.json).

## User-defined structs, Data Tables, and Data Assets

Create a user-defined struct with one to 64 complete member declarations. Members use the shared canonical K2 type/default forms and retain stable Unreal GUID identities across rename and safe reorder operations. Add, rename, default update, and reorder compile the structure before saving; type changes and removals require `"policy": "reject_if_referenced"` and reject when the bounded dependency scan finds a Data Table, Blueprint, or other package referencer.

Create a Data Table from one exact live native `FTableRowBase` descendant or user-defined struct, then inspect its reflected schema and sorted rows. Reads accept at most 64 exact `row_names`; pagination cursors are single-use and remain bound to the query-independent asset snapshot. Inspection supports `FGameplayTag`, `FGameplayTagContainer`, `FGuid`, typed `FGameplayAttribute` values including map keys, `FText`, enum values, compatible hard/soft object and class references, arrays including soft references, sets, maps, and bounded nested reflected structs. If one field is unsupported, that field is marked with an `unsupported` sentinel while supported sibling fields remain available. Row writes retain the narrower validated codec and reject instanced object graphs, delegates, interfaces, transient/editor-only references, unknown fields, incompatible values, and unrestricted serialization text.

Use `target: "data_asset"` for generic read-only inspection of one exact `UDataAsset` or `UPrimaryDataAsset`. Optional `property_names` selects at most 64 exact reflected editable properties; references to Data Tables and other compatible assets are returned as paths. Unsupported properties remain visible with `supported: false`, while mutation and arbitrary non-Data-Asset UObject inspection remain unavailable.

`add_row`, `replace_row`, `rename_row`, and `remove_row` affect one named row. A mixed `batch` stages at most 64 combined upserts and removals before opening its transaction, so duplicate names, case conflicts, missing fields, overlaps, or one invalid value reject without partial changes. `preserve_unspecified: true` is explicit and valid only for an existing row; otherwise omitted fields take the row struct's live defaults.

Every accepted game-data edit saves non-interactively and returns the new snapshot plus concise changed-name read-back. Unexpected save or read-back failure restores and re-saves the prior state. The project-neutral data-table sequence in [`examples/game-data-workflow.json`](../../examples/game-data-workflow.json) demonstrates schema creation, table creation, filtered inspection, a preserved partial replacement, an atomic batch, and safe schema-removal policy.

## Multiplayer authoring and framework assignment

Actor and GameState families publish their exact supported replication settings in `family_capabilities.multiplayer`. Set Actor replication before dependent movement or component replication:

```json
{"operation_id":"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb","asset_path":"/Game/Actors/BP_Door.BP_Door","expected_snapshot":"0123456789abcdef0123456789abcdef01234567","replication_setting":"replicates","value":true}
```

Then use `blueprint_component_edit` with `operation: "set_replication"`, a stable local `component_id`, and Boolean `replicates`. The typed Actor settings are `replicates`, `replicate_movement`, `always_relevant`, `only_relevant_to_owner`, `use_owner_relevancy`, `dormancy`, `net_priority`, `net_update_frequency`, and `min_net_update_frequency`. Movement and component replication require Actor replication; always-relevant and owner-only relevancy conflict; update-frequency bounds are checked together. Replicated variables and RepNotify retain their lifetime-condition and notification-function coupling.

Custom-event metadata accepts `rpc_mode` (`not_replicated`, `server`, `client`, or `multicast`) and `reliability` (`unreliable` or `reliable`). Reliable non-replicated events, call-in-editor RPCs, forged/conflicting network flags, unsupported family modes, and invalid signatures reject before mutation. RPC delivery still follows Unreal ownership and authority rules: server RPCs require an owning client path, client RPCs reach the owning client only, multicast RPCs originate on authority, and reliable delivery is ordered but not an unlimited queue.

`gameplay_framework_edit` requires the active 40-character `project_hash`, an `expected_class`, and a compiled saved native or Blueprint-generated class. It edits only `default_game_mode` or `default_game_instance`, reports old/new values, verifies atomic persistence, and reports `restart_required: false`; existing worlds and active PIE sessions are unaffected, and world-specific overrides must still be changed manually. Read-only/source-controlled config and failed persistence preserve the prior setting. See [`examples/multiplayer-framework-workflow.json`](../../examples/multiplayer-framework-workflow.json).

Compilation and saving remain explicit. Both require `operation_id`, `asset_path`, and the latest `expected_snapshot`. `blueprint_compile` returns `compile_succeeded: false` rather than a tool error when the compiler completed and found Blueprint errors. `blueprint_save` does not compile implicitly. Re-inspect after compile because Unreal may reconstruct identities; save only the current returned snapshot.

See [`examples/creation-workflow.json`](../../examples/creation-workflow.json), [`examples/game-mode-game-state-workflow.json`](../../examples/game-mode-game-state-workflow.json), [`examples/game-instance-workflow.json`](../../examples/game-instance-workflow.json), [`examples/multiplayer-framework-workflow.json`](../../examples/multiplayer-framework-workflow.json), [`examples/game-data-workflow.json`](../../examples/game-data-workflow.json), and the remaining focused files under [`examples/`](../../examples/) for complete inspect-before-edit/discover sequences.

If saving fails, confirm that the package directory is writable and that source-control policy has not made the existing `.uasset` read-only. If compilation fails, inspect the returned diagnostics, correct the Blueprint in the editor or through later editing phases, compile again, and save only after `compile_succeeded` becomes true.
