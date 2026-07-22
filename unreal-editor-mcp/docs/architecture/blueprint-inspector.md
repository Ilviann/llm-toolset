# Blueprint-family inspector

## Ownership

`FUnrealMCPBlueprintInspector` remains the read-only `blueprint_inspect` facade and owns only paging/cursor retention plus orchestration into the private builder. `FInspectionQuery` normalizes and validates initial requests. Discovery, overview/component/default, member, function/local, macro, custom-event, and graph collectors feed one bounded record/fingerprint sink so every requested section is emitted from one structural snapshot.

## Dependency direction

The HTTP bridge owns one inspector and supplies already-authenticated JSON arguments. The facade depends on the private inspection builder; the builder depends on its normalized query and focused collectors, the Asset Registry, public Blueprint/SCS/graph APIs, reflected property metadata, the shared property/K2 codecs, and the shared typed Blueprint-reference scanner. Collectors never depend on cursor state. The component has no dependency on MCP framing, discovery files, editor UI, transactions, compilation, or package saving.

## Invariants

- Omitting `package_path` discovers across every content mount visible to the project. Supplying it restricts discovery recursively below that normalized mount/package path. Exact optional asset-name matching, a 2,048-candidate ceiling, and no asset loading apply in either form.
- Deep inspection resolves one exact object or package path in any visible mount, rejects missing, non-Blueprint, and unpublished-family assets, and loads only that target.
- Discovery records identify the resolved published family without loading candidates. Exact inspection pages and summary records report the family plus live defaults/components/event-graph/local/override/graph-type capabilities.
- Read scope includes `/Game`, `/Engine`, enabled project plugins, enabled engine/marketplace plugins, and other mounted content. This does not grant mutation authority: mutation scope is `/Game` plus symlink-free content mounts owned by plugins physically located under the current project's local `Plugins/` directory.
- Default inspection is shallow: summary, parent, compile state, components, typed variables, functions, macros, custom events, locals, and graphs. Nodes, pins, connections, and separate parameter records require explicit sections. Stable member/function/local/macro/custom-event filters select one exact declaration and its scoped records.
- `class_defaults`, `component_id`, and `property_names` provide bounded targeted default inspection. Component records distinguish local, inherited, and native ownership and publish editability, scene/root state, and stable identity availability.
- Every result is a page from one structural snapshot. A continuation cursor is opaque, single-use, retained for 30 seconds, and bound to its normalized query and snapshot. Changed structure returns `stale_precondition`.
- At most 4,096 structural records, 100 records per page, 32 cursors, and 16 changed component defaults per component are retained or encoded. The shared 256 KiB response bound still applies.
- Inspection never opens a transaction, compiles, saves, changes selection, or intentionally marks a package dirty. The inspector checks package dirty state and compile status before returning.
- Variable records expose canonical K2 types/tagged defaults, metadata, ownership/editability, replication and validated mutable RepNotify relationships, plus at most 64 exact loaded graph/node references and an unresolved-reference signal.
- Variables, functions, locals, macros, and custom events obtain reference summaries from the same typed bounded scanner used by mutation preflight; collectors encode that result only when producing records.
- Function records distinguish user-owned, inherited, override, and interface functions. Macro records expose pure/impure signatures, metadata, required tunnel identities, graph relationships, and macro-instance references. Custom-event records remain distinct from custom-event overrides, inherited events, and native override events and expose exact event-graph placement. Parameter records preserve order/direction/reference/const/default forms. Local records carry stable GUIDs, exact function scope, type/default, editability, and scope-aware references.

## Verification

`UnrealMCP.Phase2` covers discovery, structure, paging, identity, and non-mutation. `UnrealMCP.Phase4` adds component/default read-back. `UnrealMCP.Phase5` adds exact variable inspection. `UnrealMCP.Phase6` adds function/local/RepNotify inspection. `UnrealMCP.Phase7` adds exact macro/custom-event signature, ownership, graph, required-node, and reference inspection. `UnrealMCP.Phase14` covers family classification and live capabilities for GameModeBase, GameMode, GameStateBase, and GameState before and after compile/save/restart.
