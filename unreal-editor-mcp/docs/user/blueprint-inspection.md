# Blueprint inspection

## Blueprint-family inspection

Read-only discovery covers every mounted content namespace visible to the project: project content under `/Game`, engine content under `/Engine`, and enabled plugin content under each plugin's virtual mount. Omitting `package_path` searches across all of them within the published scan ceiling. Prefer an exact, narrow mount/package filter when known:

```json
{
  "mode": "discover",
  "package_path": "/Game/Actors",
  "asset_name": "BP_Door",
  "page_size": 10
}
```

For example, an enabled plugin whose mount point is `/MyGameplayPlugin` can be searched with `"package_path": "/MyGameplayPlugin"`. Plugin mount names are not necessarily the same as their disk folder names.

Mutation tools intentionally use a narrower policy: they may change only `/Game` assets and content mounted from a plugin physically located in the current project's `Plugins/` directory. A local plugin mount must remain below a symlink-free plugin directory containing a `.uplugin` descriptor. `/Engine`, engine plugins, marketplace plugins installed outside the project, arbitrary external mounts, and symlink escapes remain read-only. `capabilities.asset_access` reports this split.

Inspect one exact asset after discovery. The shallow default returns summary, parent, compile state, components, variables, functions, macros, custom events, function-local variables, and graph summaries. Request parameter or graph details only when needed:

```json
{
  "mode": "inspect",
  "asset_path": "/Game/Actors/BP_Door.BP_Door",
  "sections": ["summary", "components", "variables", "graphs", "nodes", "pins", "connections"],
  "page_size": 50
}
```

Set `include_inherited` to include Blueprint-ancestor variables, callables, components, and native components. Stable `graph_id`, `component_id`, `member_id`, `function_id`, `local_id`, `macro_id`, and `custom_event_id` filters select one corresponding record and its related parameter records. Add one-to-32 `property_names` and the `class_defaults` section for targeted reflected-default read-back. The committed [`examples/inspection-queries.json`](../../examples/inspection-queries.json) contains focused examples.

Results are flat records with a `section` discriminator and a structural `snapshot_id`. A partial result supplies a single-use `next_cursor`; continue it within 30 seconds using only:

```json
{"cursor": "0123456789abcdef0123456789abcdef", "page_size": 50}
```

The cursor is bound to the original normalized query and snapshot. If graph structure, identities, defaults, or links change before continuation, the call returns `stale_precondition`. Re-inspect after compile, undo/redo, reload, or node reconstruction even when Unreal retained the same GUIDs.

Component, variable, function, local-variable, macro, custom-event, graph, node, and pin records use Unreal GUIDs where available and report `identity_stable: false` rather than inventing an ID otherwise. Callable records keep user functions, overrides, interfaces, macros, ordinary custom events, custom-event overrides, and inherited declarations distinct. They return complete signatures, metadata, graph relationships, required nodes, and bounded references. Supported properties use compact bounded Boolean, finite-number, string/name/text, enum/flags, common struct, and compatible visible asset/class reference encodings. Unsupported fields remain explicit with `supported: false`; arbitrary UObject graphs are never serialized.

Discovery asset records include `blueprint_family` and `native_family_class`. Every exact inspection page includes `blueprint_family` and `family_capabilities`; these fields remain present on cursor continuations. Generic Actor-derived Blueprints report `actor`. Descendants of `AGameModeBase`, `AGameMode`, `AGameStateBase`, and `AGameState` report `game_mode_base`, `game_mode`, `game_state_base`, and `game_state` respectively. `UGameInstance` descendants report `game_instance`; their summary record sets `actor_blueprint` to false.
