# Blueprint mutator contracts

Use the index to retrieve only the contract section relevant to the task.

## Creation, compile, and save contracts

`blueprint_create` accepts exactly `operation_id`, `parent_class`, and `package_path`. `parent_class` is a bounded Unreal class object path such as `/Script/Engine.Actor`, `/Script/UMG.UserWidget`, or `/Game/Actors/BP_Base.BP_Base_C`. `package_path` is a long package name without an object suffix, such as `/Game/Actors/BP_Door`.

The parent must resolve to a usable class in the explicit Actor/GameMode/GameState/GameInstance/Widget family policy. Native parents must pass Unreal's live Blueprint-base policy; the published abstract `/Script/UMG.UserWidget` base is allowed through the specialized Widget Blueprint creation path. Blueprint-generated parents must not be skeleton, reinstanced, compiling, or in an error state. Other abstract, deprecated, newer-version, editor-only, missing, and unpublished-family classes reject as `invalid_parent`.

`blueprint_compile` and `blueprint_save` each accept exactly `operation_id`, `asset_path`, and `expected_snapshot`. Package paths and canonical object paths are accepted; object names must match the package asset name. The asset must belong to a published Blueprint family inside the mutation scope. A mismatched structural snapshot returns `stale_precondition` before compile/save.

The mutation scope is `/Game` plus content mounts physically below the current project's local `Plugins/` directory and owned by an ancestor `.uplugin` descriptor. Existing path segments from the trusted root to the destination must not be symlinks. `/Engine`, external engine/marketplace plugins, arbitrary dynamic mounts, raw filesystem paths, traversal, and unavailable mounts reject as `mutation_scope_denied` or `invalid_argument`.

Every successful operation returns `operation_id`, `operation_state`, `bridge_instance_id`, `request_digest`, `asset_path`, `blueprint_family`, live `family_capabilities`, `parent_class`, `compile_state`, `compile_succeeded`, `saved`, `package_dirty`, `snapshot_id`, `diagnostics`, `diagnostic_count`, and `diagnostics_truncated`. Each diagnostic contains only `severity` (`error`, `warning`, or `note`) and a bounded `message`. At most 64 messages of 512 characters each are returned.

An explicit compiler run that finds Blueprint errors is a successful tool operation with `compile_succeeded: false`; this preserves its structured diagnostics. Creation requires a successful compile and instead returns `compile_failed` while removing the unpublished asset. Other stable mutation errors include `already_exists`, `write_conflict`, and `save_failed`. Transport, authentication, timeout, version, and response-limit errors remain distinct bridge failures.

## Component and Blueprint-default mutation contracts

`blueprint_component_edit` performs exactly one prior operation or typed `set_replication`. `blueprint_default_edit` accepts either one safe reflected property or one fixed Actor `replication_setting`. Every shape retains operation, asset, snapshot, and stable component identity preconditions.

Only locally owned Simple Construction Script components in a family that publishes component support are mutable. Inherited and native components remain inspectable and explicitly non-editable; instance components are outside the asset mutation boundary. GameInstance publishes and reports component support as false, produces no component records, and rejects every component operation as `invalid_component` before snapshot validation or a transaction. Added classes must be usable, non-abstract, non-deprecated, non-editor-only `UActorComponent` descendants with inherited `BlueprintSpawnableComponent` metadata. Names are exact, legal, and unique. Scene attachments require scene components, cycles reject, non-scene components attach to the Actor context, and a live scene root must be selected before removing a root that has alternatives.

`blueprint_default_edit` sets one safe editable property on the Blueprint generated-class default object. `set_property` uses the same codec on one local component template. Each accepted edit runs in one editor transaction, verifies its direct postcondition and authoritative inspection snapshot, returns the concise changed record and dirty state, and uses Undo for explicit restoration after an unexpected post-mutation failure. Compilation and saving remain separate reconciled operations.

Typed Actor settings cover replication, movement, relevancy, dormancy, priority, and update frequencies. Actor replication precedes movement/component replication; disabling it requires those dependents disabled. Always-relevant and owner-only conflict, numeric values are bounded, and minimum update frequency cannot exceed the main frequency.

## Reflected default-property codec

The shared property policy is used by targeted inspection, component-template writes, Blueprint class-default writes, result read-back, and structural fingerprints. A property must be a single editable template value and must not be transient, deprecated, template-disabled, a delegate/interface, or an array/set/map.

Supported JSON forms are:

- Boolean properties: JSON Boolean.
- Finite numeric properties: JSON number; integer values must be integral and within exact JSON integer range.
- name, string, and text: JSON string.
- enum: enumerator-name string; flags enums: bounded array of enumerator-name strings.
- Vector/Vector2D/Vector4, Rotator, Quat, Transform, Color/LinearColor, IntPoint/IntVector/IntVector4: bounded canonical Unreal import-text string.
- hard/soft class references: exact compatible class path string or an empty string for null.
- hard/soft object references: exact compatible visible packageable asset path string or an empty string for null.
- `FGameplayTag`: exact registered canonical tag-name string, or an empty string for an empty tag.
- `FGameplayTagContainer`: case-sensitive sorted JSON array of at most 64 explicit registered tag-name strings; derived parents are omitted.

Gameplay Tag writes reject malformed, unknown, redirected, non-canonical, duplicate, empty-container-item, over-256-character, or over-limit values before assignment. Bounded stored legacy-invalid names remain visible during inspection. References must resolve, satisfy the reflected property class, and not be transient or editor-only. Hard arbitrary UObject graphs, Actor/component instances, raw pointers, delegates, unsupported structs, and other containers reject. Inspection returns `supported: false` for a named property outside this policy rather than recursively reflecting it.

## Canonical K2 member, parameter, and local type/default codec

Member-variable inspection and mutation share one bounded K2 vocabulary. A type record contains `category`, `container`, optional `subcategory`, optional `type_object`, and, for maps only, `value_type`. Supported categories are `boolean`, `byte`, `int`, `int64`, `real`, `name`, `string`, `text`, `enum`, `struct`, `object`, `class`, `softobject`, and `softclass`. Real values require `float` or `double`; enum, struct, object, and class families require one live compatible Unreal object path. Containers are `none`, `array`, `set`, or `map`; nested containers are unavailable.

Defaults are tagged objects rather than untyped Unreal serialization strings:

- `{kind:"engine_default"}` selects the type's default value.
- `{kind:"literal",value:...}` carries one Boolean, finite number, bounded text/name/enum value, or other supported scalar literal.
- `{kind:"reference",path:"..."}` carries one compatible hard/soft object or class path; an empty path is null.
- `{kind:"array"|"set",items:[...]}` carries at most 64 scalar/reference atoms.
- `{kind:"map",entries:[{key:...,value:...}]}` carries at most 64 scalar/reference pairs.

The native codec resolves type objects and references against live K2 capabilities, rejects incompatible types/defaults, and converts accepted values to Unreal's canonical property text internally. Non-default arbitrary struct literals remain explicitly unsupported. Inspection reconstructs tagged defaults from the variable description before compile and from the generated-class CDO after Unreal migrates compiled defaults there.

Callable type records have optional `reference` and `const` flags. Member and local-variable types require both flags to be false. Function and macro inputs and custom-event parameters may be references; a const parameter must also be a reference. Function and macro outputs cannot be reference or const. Reference parameters do not accept defaults. Parameter direction, callable owner kind/identity, and local function scope are explicit in their records.

## Blueprint member-variable contracts

`blueprint_member_edit` performs one `add`, `rename`, `update`, or `remove` operation on an Actor Blueprint. Every call carries `operation_id`, exact `asset_path`, and the current `expected_snapshot`. Existing members are selected only by their stable 32-character `VarGuid`; inherited members remain inspectable but immutable.

`add` requires an exact legal cross-kind-unique name and one canonical K2 type, with optional tagged default and metadata. `rename` preserves the member identity and updates Unreal-owned variable references. `update` changes exactly one field family: `type`, `default`, or `metadata`. Type updates require `policy: "reject_if_referenced"` and reset the old incompatible default to the new type's engine default. `remove` requires the same reject-only policy. No operation cascades node deletion, leaves orphaned references, or attempts graph repair.

Supported metadata includes category, tooltip, instance editability, Blueprint visibility/read-only, expose-on-spawn, private, save-game, advanced-display, and replication mode `none`, `replicated`, or `rep_notify`. Expose-on-spawn requires a visible, non-private, instance-editable variable. Live K2 rules reject replicated sets/maps. RepNotify updates require an exact live lifetime condition and one impure user-owned zero-parameter/zero-return function. Inspection returns its stable function identity and relationship validity. Function rename preserves the coupling; invalid signature changes and coupled-function removal reject.

Every variable record reports type, tagged default, metadata, ownership/editability, replication, and a bounded reference summary with graph/node identities. Results contain the operation, concise member record, reference summary, reconstructed identities, dirty state, and new authoritative snapshot. Preflight rejection occurs before opening a transaction; accepted edits are transactional and unexpected failed read-back is restored through Undo.

## Function-signature and local-variable contracts

`blueprint_member_edit` uses `target: "function"` or `target: "local_variable"` for Phase 6 scoped members. Every operation keeps the common operation ID, exact Actor Blueprint asset path, and expected structural snapshot contract.

User functions are selected by the stable 32-character `GraphGuid`. Only locally owned editable function graphs are mutable; inherited functions, parent overrides, and interface implementations remain separately inspectable and read-only. Function `add` creates a function graph with the requested complete signature and preserves its required entry and at least one result node. `rename` preserves the graph identity and updates Unreal function references plus any RepNotify relationship. `remove` and complete-signature `update` require `reject_if_referenced`; no call nodes are deleted or repaired.

A complete signature contains access (`public`, `protected`, or `private`), pure/const flags, and at most 32 ordered parameters. Every parameter has one legal unique name, direction (`input` or `output`), and canonical K2 type. Input parameters may carry tagged defaults; reference inputs cannot. Const parameters must be references. Outputs cannot be reference/const and have no defaults. The mutator validates the whole signature before opening a transaction, applies it to the entry and every result node, reconstructs those required nodes, and verifies exact inspection afterward.

Function metadata updates support category, tooltip, keywords, and call-in-editor. Inspection returns ownership, editability, complete signature, separate parameter records, metadata, required-node identities/counts, RepNotify member relationships, and at most 64 exact call references.

Local variables are scoped by function graph identity and selected by stable `VarGuid`. `add`, identity-preserving `rename`, tagged-default `update`, reject-only type `update`, and reject-only `remove` use Unreal's public local-variable utilities. Local types use the canonical K2 vocabulary but cannot be reference or const. Inspection returns the exact function scope, type/default, ownership/editability, and a bounded scope-aware reference summary.

RepNotify metadata accepts `replication: "rep_notify"`, one exact user-owned notification-function name, and an exact live `ELifetimeCondition` name such as `COND_OwnerOnly`. The notification function must be impure and have no inputs or outputs. Inspection publishes the related function identity and relationship validity. A coupled function cannot gain a parameter/return, become pure, or be removed; renaming it updates the relationship transactionally.

## Macro and custom-event contracts

`blueprint_member_edit` uses `target: "macro"` and `target: "custom_event"` for Phase 7 callable shells. Every request keeps the common operation ID, exact Actor Blueprint asset path, and expected structural snapshot contract.

Macros are selected by their stable 32-character graph GUID. Only locally owned macro graphs are mutable. A complete macro signature contains `pure` plus at most 32 ordered parameters. Macro parameters carry name, direction (`input` or `output`), canonical K2 type, and an optional tagged default for non-reference inputs. Outputs cannot be reference/const or carry defaults. Impure signatures own one execution input and output in addition to their declared data parameters. Add and signature update preserve the required editable entry and exit tunnel nodes. Metadata supports bounded category, tooltip, and keywords.

Custom events are selected by their stable node GUID and one local event graph. Metadata supports bounded category, tooltip, keywords, call-in-editor, `rpc_mode`, and `reliability`. RPC modes are not replicated, server, owning client, or multicast; only replicated modes may be reliable. Family mode support is live-validated, RPCs cannot be call-in-editor events, and conflicting/forged flags reject.

Rename preserves the macro graph or custom-event node identity. Complete-signature updates and removal require `reject_if_referenced`. Macro instances and custom-event call nodes produce `referenced_member`; the mutator never deletes, reconstructs, or retargets those callers. Every accepted operation runs in one editor transaction, performs exact inspection read-back, returns the callable record and reference summary, and reports reconstructed or created identities without conflating functions, interfaces, override events, macros, and custom events.

## Blueprint reference scanner

`UnrealMCPBlueprintReferenceScanner` is a private native collaborator shared by Blueprint inspection and mutation. It accepts one already-resolved Blueprint plus a typed variable, function graph, local scope, macro graph, or custom-event target and returns `FScanResult`; it never accepts JSON and never resolves model-supplied names by itself.

`FScanResult` carries the authoritative referenced flag, exact loaded-node count, unresolved-reference flag, truncation flag, and at most `MaxVariableReferences` typed node records. Records contain bounded graph/node identities, node class, and display title and are sorted by graph then node identity. Mutation uses the typed flags/count directly for reject-only policy. `Encode` is called only when inspection or a mutation result crosses the JSON wire boundary.
