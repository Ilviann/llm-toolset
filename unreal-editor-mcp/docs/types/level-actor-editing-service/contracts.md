# Level actor editing contracts

## Edit request and operation matrix

`level_actor_edit` requires one 32-lowercase-hex `operation_id`, the exact 40-hex current `map_id`, its exact 40-hex `expected_snapshot`, and 1–32 operations. Existing `actor_id` values are `<map_id>:<32-hex Actor GUID>`; component IDs are the stable 32-hex identities returned by `level_inspect`.

| Operation | Required payload | Optional payload | Exact effect |
| --- | --- | --- | --- |
| `spawn` | `class_path`, complete `transform` | `label`, `tags`, `folder`, `data_layers`, `actor_properties` | Spawn one concrete native or Blueprint Actor in the persistent level |
| `transform` | `actor_id`, complete `transform` | — | Replace world location, rotation, and scale |
| `label` | `actor_id`, `label` | — | Replace the editor label |
| `tags` | `actor_id`, `tags` | — | Replace the complete Actor tag set |
| `folder` | `actor_id`, `folder` | — | Replace the editor folder; empty means root |
| `data_layers` | `actor_id`, `data_layers` | — | Replace the complete Data Layer instance set |
| `attach` | `actor_id`, `parent_actor_id` | `socket_name` | Attach while preserving world transform |
| `detach` | `actor_id` | — | Detach while preserving world transform |
| `actor_property` | `actor_id`, `property_name`, `value` | — | Set one supported exposed instance property |
| `component_property` | `actor_id`, `component_id`, `property_name`, `value` | — | Set one supported exposed component property |
| `move` | `actor_id`, `target_level` | — | Move to an exact loaded non-World-Partition level package while preserving GUID |
| `delete` | `actor_id` | — | Delete one supported actor through Unreal editor APIs |

Transforms contain exact `location{x,y,z}`, `rotation{pitch,yaw,roll}`, and `scale{x,y,z}` records. Tags are unique, bounded to 64; Data Layers to 32; reflected assignments to 32; actors/components resolved by a batch to 64. String, numeric, collection, and recursive value bounds follow the shared property codec.

Operation order is observable. For example, Unreal attachment adopts the parent editor folder, so put an explicit `folder` operation after `attach` when that folder is the intended final state. A result contains ordered read-back records, unchanged existing identities, created identities/packages, the advanced snapshot, and at most 64 exact affected packages.

## Class and property policy

`class_path` is one exact mounted native class path or Blueprint generated-class path. Missing, non-Actor, abstract, deprecated, superseded, dynamically transient, or editor-only classes are refused. Classes are never found by short name, arbitrary asset search, source code, or runtime download.

Properties must be one direct field name with no traversal. The shared codec accepts only visible instance-editable non-transient, non-deprecated, non-editor-only scalar/enum/struct/soft-reference values and bounded supported collections. Class/default-only, disabled-on-instance, delegates, object internals, nested paths, unsupported containers, and incompatible values are refused during full-batch prevalidation.

## Transactions, loading, and recovery

The service resolves and validates the whole batch before starting one editor transaction. It rejects duplicate field writes, delete-plus-other-operation conflicts, unavailable World Partition actors/cells, locked Data Layers or parents, invalid sockets/levels, attachment cycles, stale state, and PIE/simulation/save/load/compile/GC/Undo/Redo/transaction conflicts.

The current map must resolve under symlink-free `/Game` project content or a local project-plugin content mount. Engine, marketplace/Engine plugin, external, and unowned mounts remain inspectable but cannot be edited or saved.

Only explicitly named World Partition actors are loaded through scoped references; the service does not load a region or the whole world. A runtime failure invokes Undo and verifies the bounded journal for pre-existing actors, newly created GUIDs, and original package dirty flags. A committed result remains one normal Unreal Undo/Redo unit and is not persisted until `level_save` succeeds. Reconcile a lost response with `operation_status` and the original operation/bridge IDs; never retry under a new ID before reconciliation.

## Save request, evidence, and verification

`level_save` requires a new operation ID, exact current map ID and snapshot, the unique 1–64 `affected_packages` returned by the edit, and `verification:{mode,actors}` with 1–32 actor expectations. `mode` is `inspect` or `reload`. Each actor expectation requires `actor_id` and may assert label, complete transform, complete tag set, folder, actor properties, or component identities/properties.

The root map must be explicit. Every package must be loaded and owned by a current loaded level as its root, external actor, or external object package. Read-only storage and unsafe editor state reject before saving. Per-package results report `save_succeeded`, `storage_present`, `deleted_from_storage`, `clean`, and `verified`; deleted external packages verify only when Unreal reports a clean empty package and storage is absent.

The result separately lists saved and failed packages and reports whether reload occurred and requested state verified. Failed persistence or state read-back produces `operation_state:"partial"` plus bounded verification diagnostics. Inspect `operation_status`, fresh `level_inspect`, storage after restart, and the exact package results before choosing recovery; the tool never claims filesystem atomicity or silently retries a partial set.

## Published limits

Runtime `capabilities.limits` is authoritative. Defaults are 32 operations, 64 resolved actors, 64 save packages, 64 Actor tags, 32 Data Layers, 64 components per Actor, and 32 reflected property names/assignments or verification actors/components where their schemas apply.
