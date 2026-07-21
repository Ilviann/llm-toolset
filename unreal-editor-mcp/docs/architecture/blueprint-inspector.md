# Actor Blueprint inspector

## Ownership

`UnrealMCPBlueprintInspector` owns the read-only `blueprint_inspect` command after the bridge dispatches it to the Game thread. It validates the native argument shape, performs bounded Asset Registry discovery across project-visible content mounts, resolves one exact mounted asset, loads only that requested asset for deep inspection, encodes Blueprint structure, computes structural snapshots, and owns short-lived cursor state.

## Dependency direction

The HTTP bridge owns one inspector and supplies already-authenticated JSON arguments. The inspector depends on the Asset Registry, public Blueprint/SCS/graph APIs, reflected property metadata, the shared property codec, and executable limits. It has no dependency on MCP framing, discovery files, editor UI, transactions, compilation, or package saving. Python publishes the same exact query shapes before forwarding them.

## Invariants

- Omitting `package_path` discovers across every content mount visible to the project. Supplying it restricts discovery recursively below that normalized mount/package path. Exact optional asset-name matching, a 2,048-candidate ceiling, and no asset loading apply in either form.
- Deep inspection resolves one exact object or package path in any visible mount, rejects missing, non-Blueprint, and non-Actor assets, and loads only that target.
- Read scope includes `/Game`, `/Engine`, enabled project plugins, enabled engine/marketplace plugins, and other mounted content. This does not grant mutation authority: mutation scope is `/Game` plus symlink-free content mounts owned by plugins physically located under the current project's local `Plugins/` directory.
- Default inspection is shallow: summary, parent, compile state, components, variables, and graphs. Nodes, pins, and connections require explicit sections.
- `class_defaults`, `component_id`, and `property_names` provide bounded targeted default inspection. Component records distinguish local, inherited, and native ownership and publish editability, scene/root state, and stable identity availability.
- Every result is a page from one structural snapshot. A continuation cursor is opaque, single-use, retained for 30 seconds, and bound to its normalized query and snapshot. Changed structure returns `stale_precondition`.
- At most 4,096 structural records, 100 records per page, 32 cursors, and 16 changed component defaults per component are retained or encoded. The shared 256 KiB response bound still applies.
- Inspection never opens a transaction, compiles, saves, changes selection, or intentionally marks a package dirty. The inspector checks package dirty state and compile status before returning.

## Verification

`UnrealMCP.Phase2` covers discovery, structure, paging, identity, and non-mutation. `UnrealMCP.Phase4` adds local/native component classification and targeted component/class-default read-back through the shared codec. The cross-process script restarts the editor and compares persisted snapshots and edited defaults through the production Python client.
