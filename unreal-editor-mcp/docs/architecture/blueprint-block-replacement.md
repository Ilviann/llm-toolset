# Blueprint block replacement

## Ownership

`FUnrealMCPBlueprintBlockReplacementService` owns `blueprint_block_replace`. Request decoding and bounded plan validation live in `UnrealMCPBlueprintBlockReplacementRequest`; `UnrealMCPBlueprintLogicUnitFingerprint` owns function, macro, custom-event, and native-event boundary discovery and fingerprints. The service composes the existing inspector, family policy, action catalog, K2 value codec, graph result encoder, operation ledger, compiler, and mutation rollback helpers.

## Dependency direction

The bridge admits and dispatches the retained mutation. The service resolves the exact live Blueprint and logic-unit root, verifies its complete boundary including external crossing links, and asks the catalog to batch-resolve action identities. It duplicates the Blueprint into a non-transient `/Temp` package, rebinds regenerated scratch root/external GUIDs to exact equivalent nodes and pins, applies the semantic plan there, compiles it, destroys all scratch objects, rechecks the live snapshot, then applies the same plan to the live graph in one transaction. The inspector and catalog do not depend on the replacement service.

## Invariants

- One complete editable user function, local macro, custom-event handler, or native-event-rooted handler is supported; arbitrary regions, inherited/interface/override functions, multiple-result function boundaries, and ambiguous roots reject.
- Function and macro ownership covers the complete body between required boundary nodes. Event ownership follows execution from one root, cuts at shared control joins, and includes only private pure data dependencies whose outputs are not shared.
- Entry, optional result, old owned-node, local-variable, external-link, Blueprint snapshot, and logic-unit-fingerprint preconditions must exactly match the latest inspection record.
- Body nodes come only from unexpired context-free action IDs bound to that graph snapshot. Every changed or conversion node either has an explicit position or receives one exact `layered_v1` scratch layout position; the two contracts cannot mix. Boundary links are direct and bounded; any conversion is an internal plan connection.
- Boundary links and the old body are cleared only in scratch or inside the live transaction. Declarations, tunnel/root semantics, locals, metadata, graph identity, shared external nodes, and unrelated Blueprint content are preserved.
- Scratch compile must succeed and must not alter the planned semantic fingerprint. Scratch objects are removed before the live snapshot is checked again.
- Live output must match the compiled scratch fingerprint and authoritative inspection. Automatic layout reuses scratch positions rather than replanning live, and an untouched-graph fingerprint verifies unrelated nodes, positions, comments, defaults, and links. Unexpected failure undoes the transaction and verifies the exact prior snapshot, dirty state, and compile status.
- Results are retained by `operation_id`; a lost response is reconciled through `operation_status`, and same-request replay does not execute again.

## Verification

Run the Python suite, normal and forced-unity Editor builds, `UnrealMCP.FunctionReplace`, `UnrealMCP.EventMacroReplace`, the full `UnrealMCP` Automation suite, and the cross-process headless workflow. Native coverage exercises ownership cuts, external links, scratch compile failure, stale boundaries, live failure rollback, one-transaction Undo/Redo, compilation, saving, and exact snapshots.
