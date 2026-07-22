# Python MCP server

## Ownership

`unreal_editor_mcp/` owns the Python 3.10+ process. `stdio.py` bounds newline-delimited JSON-RPC and keeps stdout protocol-only. `server.py` negotiates MCP, publishes the fifteen Phase 17 tools, validates arguments, and converts domain failures to MCP tool errors. `project.py`, `platforms.py`, and `discovery.py` resolve one project and validate generated state. `bridge.py` is the only HTTP client. `cli.py` composes these responsibilities.

## Dependency direction

The CLI constructs a `ProjectLayout`, `UnrealBridge`, and `MCPServer`; the transport depends only on the server protocol. The server depends on an injected bridge protocol, not the concrete project or HTTP implementation. Discovery depends on an injected platform adapter so macOS, Windows, and Linux process/path behavior can be tested on one host. Everything uses the standard library.

## Invariants

- Only the thirteen prior commands plus `game_data_inspect` and `game_data_edit` appear in the tool catalog.
- Tool arguments are exact objects with no additional fields.
- `blueprint_inspect` has three mutually exclusive shapes: discovery, exact inspection, or cursor continuation; Python bounds paths, sections, cursor size, and page size before HTTP.
- `blueprint_action_catalog` requires an exact asset, graph, and snapshot and bounds exact text/owner/function/member/family filters, optional node/pin context, and result count before HTTP.
- `blueprint_graph_edit` has exact node-lifecycle, typed pin-default, and connect/disconnect shapes with stable graph/action/node/pin identities and bounded positions/defaults. Only `connect_pins` accepts optional Boolean `automatic_conversion`.
- Every mutation requires a caller-generated 32-lowercase-hex `operation_id`. Existing-asset mutations also require the current 40-lowercase-hex `expected_snapshot`.
- Component operations use one exact discriminated shape; class/component property edits accept only the bounded shared value forms.
- Member operations use exact add/rename/update/remove shapes with canonical K2 type/default records, stable identities, and reject-only signature/type/removal policies. Scoped discriminators cover functions, locals, macros, and custom events without adding another model-facing tool; custom-event add requires one stable event-graph identity.
- Custom-event metadata has exact RPC mode/reliability fields. Actor/component replication uses bounded setting discriminators, and framework assignment requires project hash plus expected current class.
- Game-data inspection has exact struct/table/cursor shapes. Game-data edits discriminate schema and row operations, bound nested value depth/collections/fields/batches, and require snapshots for existing assets.
- HTTP always targets the literal IPv4 loopback address and authenticates with the generated token.
- Generated records and HTTP messages are read with explicit byte limits and strict record shapes.
- A stale heartbeat, dead process, unsafe token format, project identity change, timeout, or version mismatch produces a stable bounded error. A mutation HTTP timeout becomes `outcome_unknown`, prompting `operation_status` reconciliation.
- `close()` closes active HTTP connections so stdio EOF cancels bounded work.

## Verification

Run `python3 -m unittest discover -s tests -v`. Changes to metadata, tool registration, discovery, HTTP, schema, errors, or stdio must update their focused tests and `test_contracts.py`.
