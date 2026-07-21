# Actor Blueprint mutator

## Ownership

`UnrealMCPBlueprintMutator` owns the six released mutation commands after operation admission and Game-thread dispatch. It validates exact native shapes and live preconditions, resolves Actor Blueprint assets plus component/member identities, edits the local SCS hierarchy, generated-class/component defaults, typed member variables, user-owned function shells/signatures, and function-local variables, captures compiler diagnostics, saves without dialogs, cleans up failed unpublished creations, and asks `UnrealMCPBlueprintInspector` for authoritative read-back snapshots.

## Dependency direction

The HTTP bridge owns one inspector and constructs the mutator with a reference to it. The mutator depends on public Kismet creation/compiler/member APIs, Subobject Data and SCS APIs, the shared reflected-property and K2 type/default codecs, transactions, package saving, the Asset Registry, mounted-package conversion, project/plugin paths, and platform file metadata. The inspector does not depend on the mutator. Compile and save collaborators are injectable only for deterministic native failure tests; production composition uses the public Unreal implementations.

## Invariants

- `blueprint_create` accepts one native or Blueprint-generated Actor parent class path and one destination long package name. It rejects unsuitable, abstract, deprecated, skeleton, reinstanced, editor-only, missing, non-Actor, or compile-error parents before package creation.
- Mutation is confined to `/Game` and mounted content beneath a symlink-free local project-plugin directory containing a `.uplugin` descriptor. Engine, external-plugin, unavailable, and symlink-escaping mounts reject.
- Existing loaded objects, packages, registry assets, or package files reject as `already_exists`, including case-only collisions on case-insensitive hosts. Creation never chooses a new name and never overwrites.
- Initial compilation and package saving finish before registry publication. Compile, save, or read-back failure deletes only the newly created file, removes any publication, moves the failed package out of the requested namespace, and marks its objects for collection so the same destination can be retried.
- Explicit compilation reports Blueprint compiler errors as `compile_succeeded: false` with at most 64 diagnostics rather than converting a completed compiler run into a transport error. Mandatory initial compilation failure returns `compile_failed` and cleans up.
- Package saving is non-interactive. A pre-existing read-only file or unwritable existing directory returns `write_conflict`; an attempted save that fails returns `save_failed`.
- Component edits add/remove/rename/reparent/set-root/set-property one local editable component by stable ID. Native and inherited components remain inspectable but immutable. Class defaults edit one supported property on the generated CDO.
- Member edits add, identity-preserving rename, update, or safely remove one local `VarGuid` member. Types/defaults use the canonical K2 codec; metadata and replication are live-validated; type changes and removals use only `reject_if_referenced`.
- Scoped member edits add/rename/update/remove one user-owned function graph or one stable local variable. Complete signatures are prevalidated, required entry/result nodes are preserved, call references reject signature change/removal, and locals use public scope-aware Blueprint utilities.
- RepNotify changes require one valid impure zero-parameter/zero-return user function and live lifetime condition. Coupled signature/removal changes reject; function rename preserves the relationship.
- Each accepted edit uses one editor transaction, checks its structural snapshot before mutation, verifies the postcondition through authoritative inspection, and explicitly undoes an unexpected failure. Compilation and saving remain separate operations.
- Every mutation result reports operation identity/state, the exact asset, new snapshot, dirty state, and a concise change record; creation/compile/save also return bounded diagnostics.

## Verification

Run the Python suite, compile the disposable Editor target, run all `UnrealMCP` Automation Tests, and run the complete cross-process script. Phase 4 covers component/default behavior. Phase 5 covers member types/defaults/metadata/reference safety. Phase 6 adds function flags/directions/reference forms, signature/local collisions and reference rejection, required nodes, RepNotify coupling, Undo/Redo, compile/save, and restart read-back. The cross-process test deliberately loses one component response, reconciles it, then verifies the complete saved authoring contract after restart.
