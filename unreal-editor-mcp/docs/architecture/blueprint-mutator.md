# Blueprint-family mutator

## Ownership

`FUnrealMCPBlueprintMutator` remains the bridge-facing facade for the six released mutation commands after operation admission and Game-thread dispatch. Lifecycle/create/compile/save, component/default, member-variable, function, local-variable, macro, and custom-event behavior live in separate `UnrealMCPBlueprintMutator*.cpp` translation units. Private mutation-common and callable-support headers own shared target, scope, snapshot, result, signature, metadata, and read-back helpers without expanding the module API.

## Dependency direction

The HTTP bridge owns one inspector and constructs the mutator facade with a reference to it. Family units depend inward on the private mutation support layer, shared reflected-property and K2 type/default codecs, and `UnrealMCPBlueprintReferenceScanner`; the inspector does not depend on the mutator. Compile and save collaborators remain injected only through the facade for deterministic native failure tests; production composition uses the public Unreal implementations.

## Invariants

- `blueprint_create` accepts one native or Blueprint-generated parent from the explicit published family policy and one destination long package name. It rejects unsuitable, abstract, deprecated, skeleton, reinstanced, editor-only, missing, unpublished-family, or compile-error parents before package creation.
- Creation and every existing-asset mutation resolve family eligibility before changing state and return the exact `blueprint_family` plus live family capabilities.
- Mutation is confined to `/Game` and mounted content beneath a symlink-free local project-plugin directory containing a `.uplugin` descriptor. Engine, external-plugin, unavailable, and symlink-escaping mounts reject.
- Existing loaded objects, packages, registry assets, or package files reject as `already_exists`, including case-only collisions on case-insensitive hosts. Creation never chooses a new name and never overwrites.
- Initial compilation and package saving finish before registry publication. Compile, save, or read-back failure deletes only the newly created file, removes any publication, moves the failed package out of the requested namespace, and marks its objects for collection so the same destination can be retried.
- Explicit compilation reports Blueprint compiler errors as `compile_succeeded: false` with at most 64 diagnostics rather than converting a completed compiler run into a transport error. Mandatory initial compilation failure returns `compile_failed` and cleans up.
- Package saving is non-interactive. A pre-existing read-only file or unwritable existing directory returns `write_conflict`; an attempted save that fails returns `save_failed`.
- Component edits add/remove/rename/reparent/set-root/set-property one local editable component by stable ID. Native and inherited components remain inspectable but immutable. Class defaults edit one supported property on the generated CDO.
- Member edits add, identity-preserving rename, update, or safely remove one local `VarGuid` member. Types/defaults use the canonical K2 codec; metadata and replication are live-validated; type changes and removals use only `reject_if_referenced`.
- Scoped member edits add/rename/update/remove one user-owned function, local variable, macro, or custom event. Complete signatures are prevalidated; function entry/results, macro tunnels, and custom-event event-graph placement are preserved. Call/macro references reject signature change/removal, and locals use public scope-aware Blueprint utilities.
- Reference safety uses one typed bounded scanner for member variables, functions, locals, macros, and custom events. Mutation policy reads the typed result directly; JSON reference summaries are created only for returned wire records.
- RepNotify changes require one valid impure zero-parameter/zero-return user function and live lifetime condition. Coupled signature/removal changes reject; function rename preserves the relationship.
- Each accepted edit uses one editor transaction, checks its structural snapshot before mutation, verifies the postcondition through authoritative inspection, and explicitly undoes an unexpected failure. Compilation and saving remain separate operations.
- Every mutation result reports operation identity/state, the exact asset, new snapshot, dirty state, and a concise change record; creation/compile/save also return bounded diagnostics.

## Verification

Run the Python suite, compile the disposable Editor target with normal/adaptive unity and forced unity, run all `UnrealMCP` Automation Tests, and run the complete cross-process script. Phase 4 covers component/default behavior, Phase 5 variables, Phase 6 functions/locals/RepNotify, Phase 7 macro/custom-event signatures, and Phase 14 all four GameMode/GameState families through defaults, components, members, framework actions, compile/save, and restart read-back.
