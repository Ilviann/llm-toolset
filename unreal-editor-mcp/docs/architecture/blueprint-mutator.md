# Actor Blueprint creation, compile, and save

## Ownership

`UnrealMCPBlueprintMutator` owns the three Phase 3 mutation commands after the bridge dispatches them to the Game thread. It validates exact native argument shapes, resolves parent classes and existing Blueprint assets, enforces mutation mounts, performs no-overwrite creation, captures bounded compiler diagnostics, saves packages without dialogs, cleans up failed unpublished creations, and asks `UnrealMCPBlueprintInspector` for the authoritative read-back snapshot.

## Dependency direction

The HTTP bridge owns one inspector and constructs the mutator with a reference to it. The mutator depends on public Kismet creation/compiler APIs, package saving, the Asset Registry, mounted-package conversion, project/plugin paths, and platform file metadata. The inspector does not depend on the mutator. Compile and save collaborators are injectable only for deterministic native failure tests; production composition uses the public Unreal implementations.

## Invariants

- `blueprint_create` accepts one native or Blueprint-generated Actor parent class path and one destination long package name. It rejects unsuitable, abstract, deprecated, skeleton, reinstanced, editor-only, missing, non-Actor, or compile-error parents before package creation.
- Mutation is confined to `/Game` and mounted content beneath a symlink-free local project-plugin directory containing a `.uplugin` descriptor. Engine, external-plugin, unavailable, and symlink-escaping mounts reject.
- Existing loaded objects, packages, registry assets, or package files reject as `already_exists`, including case-only collisions on case-insensitive hosts. Creation never chooses a new name and never overwrites.
- Initial compilation and package saving finish before registry publication. Compile, save, or read-back failure deletes only the newly created file, removes any publication, moves the failed package out of the requested namespace, and marks its objects for collection so the same destination can be retried.
- Explicit compilation reports Blueprint compiler errors as `compile_succeeded: false` with at most 64 diagnostics rather than converting a completed compiler run into a transport error. Mandatory initial compilation failure returns `compile_failed` and cleans up.
- Package saving is non-interactive. A pre-existing read-only file or unwritable existing directory returns `write_conflict`; an attempted save that fails returns `save_failed`.
- Every success reports the exact object path, parent class, compile state, dirty state, saved/compiled flags, structural snapshot, and bounded diagnostics. Detailed structure still requires `blueprint_inspect`.

## Verification

Run the Python suite, compile the disposable Editor target, run `UnrealMCP.Phase3`, and run the complete cross-process script. The native tests cover native/generated/skeleton/invalid parents, duplicate and case-only destinations, package syntax, engine/external/local-plugin mounts, read-only targets, explicit compile/save, injected failures, cleanup-and-retry, diagnostics, and preservation of existing assets. The cross-process test creates through the production Python bridge, compiles, saves, restarts the editor, and compares the reloaded parent and snapshot.
