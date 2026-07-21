# Unreal Editor MCP history

## 0.2.1 — 2026-07-21

- Expanded read-only Actor Blueprint discovery and exact inspection from `/Game` to every mounted content namespace available to the project, including `/Engine` and enabled plugin content.
- Published the durable asset-access split: reads cover all mounted content, while future mutations are confined to `/Game` and content plugins physically installed under the project's local `Plugins/` directory.
- Added native coverage using a dynamically registered plugin-style mount and updated cross-process capability checks and examples.

## 0.2.0 — 2026-07-21

- Added exact, bounded `/Game` Actor Blueprint discovery through the Asset Registry without loading discovery candidates.
- Added targeted read-only inspection for summary, parent, compile state, component hierarchy and changed defaults, variables, graphs, nodes, pins, and connections, including optional inherited Blueprint content.
- Added Unreal GUID identities, structural snapshots, 30-second opaque single-use cursors, exact graph filters, shallow defaults, page/scan/structure ceilings, and explicit unsupported-value records.
- Added native proof that inspection preserves dirty, compile, selection, and transaction state, plus behavioral coverage for wrong types, empty and oversized graphs, cursor expiry/staleness, undo/compile/save identity behavior, and fresh-editor reload equality.
- Added context-efficient inspection examples and synchronized Python/plugin release metadata at 0.2.0.

## 0.1.0 — 2026-07-21

- Added the dependency-free Python stdio MCP server with bounded framing, schema validation, discovery, authenticated HTTP calls, structured errors, timeouts, cancellation, and exact-version handling.
- Added the Unreal 5.8 editor plugin with fail-closed per-project credentials, loopback-only HTTP routing, bounded authenticated commands, Game-thread dispatch, discovery heartbeat, and clean route/state teardown.
- Added the read-only `capabilities` and `editor_state` tools; no mutation command is registered.
- Compiled public API probes for HTTPServer, transactions, Kismet and Blueprint utilities, Subobject Data, K2 schema/actions, compiler diagnostics, Asset Registry, and package saving.
- Added Python unit tests, Unreal Automation Tests, and a cross-process macOS acceptance test.
