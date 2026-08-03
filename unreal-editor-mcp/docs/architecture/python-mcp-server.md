# Python MCP server

## Ownership

`unreal_editor_mcp/` owns the Python 3.10+ process. `stdio.py` bounds newline-delimited JSON-RPC and keeps stdout protocol-only. `server.py` negotiates MCP, publishes an access-filtered catalog with independently optional lifecycle control, validates arguments, composes local and native capability fields, and converts domain failures to MCP tool errors. `tool_catalog.py` is the one ordered, access-classified catalog assembler over the core, asset, level, Blueprint, widget, gameplay-framework, game-data, and lifecycle definitions in `tool_catalog_families/`. `project.py`, `platforms.py`, and `discovery.py` resolve one project, derive its non-path identity, and validate generated state. `bridge.py` is the only HTTP client. `lifecycle.py` owns configured editor process orchestration and durable lifecycle records. `cli.py` composes these responsibilities.

## Dependency direction

The CLI constructs a `ProjectLayout`, its immutable `ProjectIdentity`, an `UnrealBridge`, and `MCPServer`; the transport depends only on the server protocol. The server depends on the ordered catalog assembler, injected identity value, and injected bridge protocol, not the concrete project layout or HTTP implementation. Tool-family definitions depend only on their shared private schema fragments. Discovery depends on an injected platform adapter so macOS, Windows, and Linux process/path behavior can be tested on one host. Everything uses the standard library.

## Invariants

- Readonly is the default. Its exact ordered catalog is `capabilities`, `editor_state`, `operation_status`, `asset_references`, `level_inspect`, `level_open`, `blueprint_inspect`, `blueprint_action_catalog`, and `game_data_inspect`.
- Writable mode inserts `operation_cancel` after `operation_status` and preserves the established family order for all other tools, producing 25 tools. Independently configured lifecycle appends `editor_lifecycle`, producing 10 readonly-with-lifecycle or 26 writable-with-lifecycle tools.
- `capabilities` always reports the configured descriptor-stem project name/hash, Python/protocol metadata, authoritative `access_mode`, lifecycle availability, and `native_capabilities_available`. Only an active authenticated bridge contributes native version, command, feature, limit, listener, asset-access, and Blueprint-family fields. Native command/family fields do not override MCP access. `editor_unavailable` produces the explicit partial response; every other bridge error remains an error.
- The public catalog order is assembled once from explicitly classified, disjoint family tuples; every tool name is unique. A call to a tool omitted by access or lifecycle configuration fails as unknown before bridge dispatch or argument validation.
- `asset_references` has exact mounted-object-path and cursor-continuation shapes with bounded page size.
- `level_inspect` has exact mounted discovery, current-map, actor-list, actor, component, and cursor-continuation shapes. Actor queries require an exact current map identity and snapshot; exact actor/component properties are requested by bounded names. `level_open` requires one 32-hex operation ID and one exact mounted World object path. `level_manage` has exact blank-create, template-create, and current-map configure shapes with explicit snapshots, opening/reload choices, and bounded settings. `level_actor_edit` requires one exact current map/snapshot and 1–32 discriminated operations; `level_save` requires its returned explicit package set and bounded exact verification expectations.
- Tool arguments are exact objects with no additional fields.
- `blueprint_inspect` has three mutually exclusive shapes: discovery, exact inspection, or cursor continuation; Python bounds paths, sections, cursor size, and page size before HTTP.
- `blueprint_action_catalog` requires an exact asset, graph, and snapshot and bounds exact text/owner/function/member/family filters, optional node/pin context, and result count before HTTP.
- `blueprint_graph_edit` has exact node-lifecycle, typed pin-default, and connect/disconnect shapes with stable graph/action/node/pin identities and bounded positions/defaults. Only `connect_pins` accepts optional Boolean `automatic_conversion`.
- `blueprint_block_replace` has one exact complete-function shape with snapshot, boundary, local, action, position, default, connection, conversion, and function-fingerprint preconditions.
- Every mutation requires a caller-generated 32-lowercase-hex `operation_id`. Existing-asset mutations also require the current 40-lowercase-hex `expected_snapshot`.
- Component operations use one exact discriminated shape; class/component property edits accept only the bounded shared value forms.
- Member operations use exact add/rename/update/remove shapes with canonical K2 type/default records, stable identities, and reject-only signature/type/removal policies. Scoped discriminators cover functions, locals, macros, and custom events without adding another model-facing tool; custom-event add requires one stable event-graph identity.
- `widget_tree_edit` uses thirteen exact operation shapes for tree, layout, style, property-binding, and Designer-event edits with stable widget/slot/graph/node identities; the separate widget catalog module prevents further growth of the Blueprint schema module.
- Custom-event metadata has exact RPC mode/reliability fields. Actor/component replication uses bounded setting discriminators, and framework assignment requires project hash plus expected current class.
- Game-data inspection has exact struct/table/cursor shapes. Game-data edits discriminate schema and row operations, bound nested value depth/collections/fields/batches, and require snapshots for existing assets.
- HTTP always targets the literal IPv4 loopback address and authenticates with the generated token.
- Generated records and HTTP messages are read with explicit byte limits and strict record shapes.
- A stale heartbeat, dead process, unsafe token format, project identity change, timeout, or version mismatch produces a stable bounded error. A mutation HTTP timeout becomes `outcome_unknown`, prompting readonly `operation_status` reconciliation. `operation_status` only looks up; writable `operation_cancel` performs safe cancellation.
- `close()` closes active HTTP connections so stdio EOF cancels bounded work.

## Verification

Run `python3 -m unittest discover -s tests -v`. Changes to metadata, tool registration, discovery, HTTP, schema, errors, or stdio must update their focused tests and `test_contracts.py`.
