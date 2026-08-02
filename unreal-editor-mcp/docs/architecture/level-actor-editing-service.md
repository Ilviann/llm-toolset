# Level actor editing service

## Ownership

`FUnrealMCPLevelActorEditingService` owns exact current-map actor/component mutation batches and explicit affected-package persistence verification. `FUnrealMCPLevelService` remains authoritative for map identity and snapshots, the shared property codec owns supported reflected values, and the editor bridge owns retained operation-ledger execution.

## Dependency direction

The service depends on the level service for current-state preconditions, public editor transactions and level-move APIs, scoped World Partition actor references, the Data Layer editor subsystem, and Unreal save/load APIs. It never accepts filesystem paths, discovers classes by partial name, broad-loads a World Partition map, implicitly saves an edit, or bypasses Unreal package deletion.

## Invariants

- `level_actor_edit` validates its complete maximum-32-operation plan before mutation against one exact map ID and snapshot. The current map and every saved package must resolve inside symlink-free project content or a local project-plugin content mount. Existing actor IDs are map-qualified GUIDs; component IDs use the level inspector's stable actor-scoped derivation.
- Native and Blueprint generated classes must resolve through exact mounted class paths and be concrete, compatible, current, non-editor-only classes outside the transient package. Reflected writes are single-level, instance-editable, supported by the shared bounded value codec, and reject unsafe flags.
- One editor transaction contains the batch. Conflicting writes, delete-plus-edit, missing or locked data layers, unavailable actors, unsafe editor state, and attachment cycles reject before it starts. Runtime failure invokes Undo and verifies the bounded actor/package rollback journal.
- World Partition resolution uses descriptor identities and one scoped reference per required actor. Existing GUIDs survive changes; new actors receive Unreal-managed GUIDs and external packages. Non-World-Partition `move` targets only an exact already-loaded level package in the current world.
- The result reports ordered operation read-back, the advanced current snapshot, and every loaded dirty root/external actor/external object package owned by the current world's levels. A successful edit never saves.
- `level_save` accepts only unique loaded packages from that ownership closure and requires the root map explicitly. It rejects read-only or unsafe state before persistence and returns save, storage/deletion, clean-state, and verification evidence per package.
- Missing storage is successful only when Unreal reports a clean empty package after save, which is the expected external-actor deletion case. Reload verification uses World Partition descriptor metadata where live folder objects are intentionally unloaded.
- Save/reload disagreement is a retained partial outcome with exact saved and failed package identities; cross-package filesystem atomicity is never claimed.

## Verification

Run the Python suite, adaptive and forced-unity Editor builds, `UnrealMCP.LevelEdit`, all `UnrealMCP` Automation Tests, and `scripts/run_headless_integration.py`. The focused native case owns World Partition package/save behavior; the production scenario owns replay, stale rejection, restart persistence, and bridge-level reconciliation.
