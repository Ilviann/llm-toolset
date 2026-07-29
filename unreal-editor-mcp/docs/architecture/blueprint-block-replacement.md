# Blueprint block replacement

## Ownership

`FUnrealMCPBlueprintBlockReplacementService` owns `blueprint_block_replace`. Request decoding and bounded plan validation live in `UnrealMCPBlueprintBlockReplacementRequest`; `UnrealMCPBlueprintFunctionFingerprint` owns complete user-function boundary discovery and fingerprints. The service composes the existing inspector, family policy, action catalog, K2 value codec, graph result encoder, operation ledger, compiler, and mutation rollback helpers.

## Dependency direction

The bridge admits and dispatches the retained mutation. The service resolves the exact live Blueprint/function and asks the catalog to batch-resolve action identities. It duplicates the Blueprint into a non-transient `/Temp` package, applies the semantic plan there, compiles it, destroys all scratch objects, rechecks the live snapshot, then applies the same plan to the live graph in one transaction. The inspector and catalog do not depend on the replacement service.

## Invariants

- Only one complete editable user-owned function is supported. Events, macros, interfaces, overrides, inherited functions, arbitrary regions, and multiple-result boundaries reject.
- Entry, result, old owned-node, local-variable, Blueprint snapshot, and function-fingerprint preconditions must exactly match the latest inspection record.
- Body nodes come only from unexpired context-free action IDs bound to that function snapshot. Every changed or conversion node has an explicit position.
- Entry/result links and the old body are cleared only in scratch or inside the live transaction. Parameters, result declaration, locals, metadata, graph identity, and unrelated Blueprint content are preserved.
- Scratch compile must succeed and must not alter the planned semantic fingerprint. Scratch objects are removed before the live snapshot is checked again.
- Live output must match the compiled scratch fingerprint and authoritative inspection. Unexpected failure undoes the transaction and verifies the exact prior snapshot, dirty state, and compile status.
- Results are retained by `operation_id`; a lost response is reconciled through `operation_status`, and same-request replay does not execute again.

## Verification

Run the Python suite, normal and forced-unity Editor builds, `UnrealMCP.FunctionReplace`, the full `UnrealMCP` Automation suite, and the cross-process headless workflow. Native coverage exercises scratch compile failure, stale boundaries, live failure rollback, one-transaction Undo/Redo, compilation, saving, and exact snapshots.
