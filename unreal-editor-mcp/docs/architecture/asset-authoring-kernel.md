# Asset-authoring kernel

## Ownership

`FUnrealMCPAssetAuthoringKernel` owns the shared creation and editing lifecycle for ordinary exact asset targets. It admits canonical package/object identities, bounded operation identities, writable mutation scope, destination collisions, unsafe editor state, transactions, persistence callbacks, registry publication, snapshot read-back, failed-creation cleanup, and exact rollback verification. Family and domain collaborators retain typed semantic validation, mutation, compilation/finalization, persistence implementation, and result encoding.

## Dependency direction

Blueprint, Widget, Game Data, and later asset-family adapters depend inward on the kernel through bounded request, hook, and result records. The kernel depends only on base Unreal Editor transaction, package, path, filesystem, and Asset Registry APIs; it does not depend on a family service, transport JSON, Python schemas, or companion plugins. The bridge operation ledger remains the authority for replay, conflicts, retained results, and lost-response reconciliation.

## Invariants

- Creation accepts one canonical long package name and its exact derived object path, validates an optional direct-native operation ID when present, confines writes to `/Game` or symlink-free local project-plugin content, rejects loaded/registry/storage collisions, and verifies an existing writable ancestor before creating a package.
- Creation rejects during PIE or garbage collection. A collaborator must return an asset owned by the admitted package at the exact admitted object path. Optional finalization completes before mandatory persistence, registry publication, and one 40-character read-back snapshot.
- Any creation failure after package allocation removes only the admitted file, reverses registry publication when necessary, moves the failed package out of the requested namespace, and marks its objects for collection so the exact destination can be retried.
- Editing accepts only the exact loaded asset and canonical path, requires one current 40-character snapshot, rejects stale state before opening a transaction, and optionally applies a family-specific unsafe-state check.
- One editor transaction owns each admitted edit. A semantic no-op whose snapshot stayed unchanged cancels without disturbing prior transactions. Successful edits optionally persist and must return an exact postcondition snapshot; changed-snapshot verification is explicit.
- A semantic, persistence, or read-back failure after mutation closes and undoes the exact transaction, re-persists when required, and verifies restoration against the admitted snapshot. Failure to restore exactly returns `rollback_failed`.
- Hooks exchange Unreal-native objects and errors only. The kernel is not an unrestricted reflection layer, generic UObject editor, schema owner, or wire codec.
- Released Blueprint creation, Widget Blueprint creation, Widget layout/style property editing, and user-defined-struct/Data Table creation and editing use this lifecycle without changing their MCP schemas or result envelopes.

## Verification

`UnrealMCP.AssetAuthoring.KernelLifecycle` covers collision admission, exact failed-creation cleanup, stale edits, no-ops, failed postcondition restoration, successful commit, and Undo/Redo. Phase 3 retains Blueprint and Widget creation compile/save/cleanup coverage; Phase 17 retains Game Data creation, stale editing, persistence, dependency, batch, and restart read-back coverage; the UMG Automation cases retain style/layout identity and snapshot verification. Run adaptive and true forced-unity Editor builds, all `UnrealMCP` Automation Tests, the Python suite, cross-process headless integration, and base Win64 packaging.
