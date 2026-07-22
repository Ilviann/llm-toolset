# Game-data service

## Ownership

`FUnrealMCPGameDataService` owns `game_data_inspect` and `game_data_edit`, opaque inspection cursors, user-defined struct creation/evolution, Data Table creation and row mutation, dependency preflight, transactions, package saving, and structural read-back. `UnrealMCPGameDataValueCodec` owns bounded reflected row values independently of Blueprint class/component defaults.

## Dependency direction

The HTTP bridge owns one lazily created service and admits `game_data_edit` through the shared operation ledger. The service depends on public Asset Registry, `FStructureEditorUtils`, `FDataTableEditorUtils`, package-saving, transaction, reflected-property, and user-defined-struct APIs. It reuses the canonical K2 type/default codec for struct-member declarations. The row-value codec depends only on reflected properties, live reference resolution, and K2 property-to-pin type conversion; Blueprint inspectors and mutators do not depend on game data.

## Invariants

- Targets are exactly `user_defined_struct` or `data_table`; every operation has one exact native-validated shape.
- Read access covers visible mounted assets. Mutation uses the shared `/Game` and symlink-free local-project-plugin scope policy.
- User-defined struct members use persistent `VarGuid` identities. Add, rename, type/default update, reorder, and reject-only removal are supported; every accepted result is compiled, saved, and read back.
- Type changes and removals reject if the bounded Asset Registry dependency scan finds any referencer or truncates. No dependent asset is silently rewritten by a destructive schema edit.
- Data Tables bind to one exact live native `FTableRowBase` descendant or user-defined struct. Their schema, sorted row names, and typed values contribute to one query-independent snapshot.
- Add, replace, rename, remove, and mixed batch upsert/remove operations prevalidate complete staged rows before mutation. Batch names must be unique ignoring `FName` case semantics, and upserts cannot overlap removals.
- `preserve_unspecified` is explicit and valid only when a row already exists. Otherwise omitted fields receive the live row-struct defaults.
- Row values are bounded reflected values, never unrestricted Unreal serialization text. Arbitrary instanced object graphs, interfaces, delegates, transient/editor-only references, unsupported properties, filesystem import/export, and code are rejected.
- Accepted edits save before returning and report `saved: true`, `dirty: false`, the new snapshot, and bounded changed names. Unexpected read-back/save failure performs explicit transaction restoration and re-saves restored state.
- Inspection cursors are single-use, retained for 30 seconds, and bound to the full asset snapshot even when the initial query selects named rows.

## Verification

Run Python schema/release tests, normal and forced-unity Editor builds, `UnrealMCP.Phase17`, all `UnrealMCP` Automation Tests, and the cross-process restart/read-back workflow.
