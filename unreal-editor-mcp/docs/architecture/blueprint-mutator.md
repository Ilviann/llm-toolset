# Actor Blueprint mutator

## Ownership

`UnrealMCPBlueprintMutator` owns the five Phase 4 mutation commands after operation admission and Game-thread dispatch. It validates exact native shapes and live preconditions, resolves Actor Blueprint assets and component identities, edits the local SCS hierarchy and generated-class/component defaults, captures compiler diagnostics, saves without dialogs, cleans up failed unpublished creations, and asks `UnrealMCPBlueprintInspector` for authoritative read-back snapshots.

## Dependency direction

The HTTP bridge owns one inspector and constructs the mutator with a reference to it. The mutator depends on public Kismet creation/compiler APIs, Subobject Data and SCS APIs, the shared reflected-property codec, transactions, package saving, the Asset Registry, mounted-package conversion, project/plugin paths, and platform file metadata. The inspector does not depend on the mutator. Compile and save collaborators are injectable only for deterministic native failure tests; production composition uses the public Unreal implementations.

## Invariants

- `blueprint_create` accepts one native or Blueprint-generated Actor parent class path and one destination long package name. It rejects unsuitable, abstract, deprecated, skeleton, reinstanced, editor-only, missing, non-Actor, or compile-error parents before package creation.
- Mutation is confined to `/Game` and mounted content beneath a symlink-free local project-plugin directory containing a `.uplugin` descriptor. Engine, external-plugin, unavailable, and symlink-escaping mounts reject.
- Existing loaded objects, packages, registry assets, or package files reject as `already_exists`, including case-only collisions on case-insensitive hosts. Creation never chooses a new name and never overwrites.
- Initial compilation and package saving finish before registry publication. Compile, save, or read-back failure deletes only the newly created file, removes any publication, moves the failed package out of the requested namespace, and marks its objects for collection so the same destination can be retried.
- Explicit compilation reports Blueprint compiler errors as `compile_succeeded: false` with at most 64 diagnostics rather than converting a completed compiler run into a transport error. Mandatory initial compilation failure returns `compile_failed` and cleans up.
- Package saving is non-interactive. A pre-existing read-only file or unwritable existing directory returns `write_conflict`; an attempted save that fails returns `save_failed`.
- Component edits add/remove/rename/reparent/set-root/set-property one local editable component by stable ID. Native and inherited components remain inspectable but immutable. Class defaults edit one supported property on the generated CDO.
- Each accepted edit uses one editor transaction, checks its structural snapshot before mutation, verifies the postcondition through authoritative inspection, and explicitly undoes an unexpected failure. Compilation and saving remain separate operations.
- Every mutation result reports operation identity/state, the exact asset, new snapshot, dirty state, and a concise change record; creation/compile/save also return bounded diagnostics.

## Verification

Run the Python suite, compile the disposable Editor target, run all `UnrealMCP` Automation Tests, and run the complete cross-process script. Phase 4 native coverage exercises scene/non-scene components, roots, attachments, identity-preserving rename, reparenting, cycle/duplicate/invalid-class rejection, component/class defaults, stale snapshots, undo/redo, compile, and save. The cross-process test deliberately loses one response, reconciles it, replays it, then verifies the saved hierarchy and default after restart.
