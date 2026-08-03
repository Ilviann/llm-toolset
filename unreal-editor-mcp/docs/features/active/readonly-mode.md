---
feature_id: readonly-mode
status: active
depends_on:
  - editor-restart
released_in: null
---

# `readonly-mode` — Readonly mode and explicit writable access

**Outcome:** Unreal Editor MCP starts with only inspection and non-persistent editor-state tools, while project-content mutation requires the explicit startup flag `--writable`.

**Implementation status:** Active and unreleased. The implementation and host-independent contract coverage are present. Windows is the mandatory native release platform; macOS verification is preferred follow-up work and Linux verification is out of scope.

**Depends on:**

- [`editor-restart`](../completed/editor-restart.md)

### Access and persistence contract

- Make readonly access the default. Without `--writable`, publish only `capabilities`, `editor_state`, read-only `operation_status`, `asset_references`, `level_inspect`, `level_open`, `blueprint_inspect`, `blueprint_action_catalog`, and `game_data_inspect`, plus `editor_lifecycle` only when separately configured.
- Allow readonly tools to inspect project and editor data and to change transient editor or process state. `level_open` may change the current level, and configured lifecycle operations may launch, stop, or restart the editor, but neither may implicitly save, discard, overwrite, compile, or dirty project content.
- Treat any tool that creates, deletes, edits, compiles, saves, generates, or persists an asset, map, project setting, configuration file, source file, or other project-owned content as writable. Publish those tools only when the server starts with `--writable`.
- Keep bounded operational state under `Saved/UnrealMCP/`, including discovery, cursors, lifecycle records, and retained operation results, available in readonly mode. This generated state must not be presented as permission to modify project-owned content.
- Omit unavailable tools from `tools/list`. A direct `tools/call` request for an omitted tool must return the existing stable unknown-tool protocol error without contacting the bridge.
- Publish the effective `readonly` or `writable` access mode through `capabilities`, independently from lifecycle availability. Do not infer write permission from native capabilities or from the editor being active.

### Tool-catalog refactoring

- Give every public tool definition one explicit internal access classification and assemble the advertised catalog from that classification. Keep the classification out of the model-facing tool schema and preserve one deterministic, duplicate-free public order.
- Classify `asset_delete`, `level_manage`, `level_actor_edit`, `level_save`, `blueprint_graph_edit`, `blueprint_block_replace`, `blueprint_create`, `blueprint_compile`, `blueprint_save`, `blueprint_component_edit`, `blueprint_default_edit`, `blueprint_member_edit`, `widget_tree_edit`, `gameplay_framework_edit`, and `game_data_edit` as writable.
- Split the mixed `operation_status` contract. Keep status lookup in the readonly `operation_status` tool, remove its `cancel` argument, and add a writable-only `operation_cancel` tool for cancellation of retained mutation operations.
- Keep `level_open` readonly because it changes only the current editor state and already refuses implicit save, discard, and prompts. Keep `editor_lifecycle` independent of write access because it controls only the configured editor process.
- Reject future mixed read/write model-facing tools. Extend existing inspect/edit family pairs instead, or introduce a separate narrowly named mutation tool when no writable family exists.

### Startup configuration

- Add the Boolean `--writable` startup flag. Its absence is authoritative readonly mode; it must not be enabled by an environment variable, tool call, native capability, generated record, or editor setting.
- Add `--editor-lifecycle <absolute-UnrealEditor-executable>` as the single canonical lifecycle opt-in. Supplying the validated executable both publishes `editor_lifecycle` and configures launch and restart; it does not imply `--writable`.
- Remove `--tool-mode` and `--editor` when adding `--editor-lifecycle`; do not retain aliases, translation, or a deprecation period. Existing server configurations must migrate to the new option.
- Represent lifecycle enablement and content-write access as independent configuration dimensions. Cover readonly, writable, readonly-with-lifecycle, and writable-with-lifecycle server configurations.
- Make the Windows deployment helper generate readonly LM Studio JSON by default and expose independent writable and selected-Engine lifecycle choices with the same deterministic four configurations.

### Verification

- Assert the exact ordered `tools/list` result for all four access/lifecycle combinations and prove every omitted `tools/call` is rejected before bridge dispatch.
- Exercise every readonly tool against representative assets and levels. Compare package dirtiness, asset fingerprints, project configuration, source files, and Undo history before and after success, rejection, pagination, timeout, cancellation, editor restart, and `level_open` flows.
- Test `operation_status` schema rejection of `cancel`, writable-only `operation_cancel`, retained-result lookup and cancellation, unknown and stale identities, and readonly reconciliation of prior operation outcomes without mutation.
- Test CLI parsing for default readonly behavior, `--writable`, the single `--editor-lifecycle` path, invalid or relative executables, every access/lifecycle combination, and rejection of the removed `--tool-mode` and `--editor` options on supported platform branches.
- Test Windows deployment JSON for readonly default, writable-only, lifecycle-only, and combined argument arrays plus fail-closed lifecycle-executable validation.
- Run MCP initialization, `tools/list`, and `tools/call` framing tests plus the full Python and native suites. On Windows, verify that readonly mode cannot dirty or persist project content and that lifecycle-only mode can open, stop, and restart the configured project without gaining content-write tools. Repeat the native acceptance on macOS when available and track it as non-blocking follow-up; no Linux verification is required.

### Documentation and completion gate

- Update setup examples, the complete tool catalog, capability fields, security guidance, lifecycle configuration, and migration instructions. Show readonly configuration first and mark `--writable` as an explicit trust decision for a dedicated project root.
- Complete and release the feature only when readonly is the tested default, every project-content mutation tool is both absent and undispatchable without `--writable`, transient editor-state tools remain usable, the lifecycle option works without another enabling flag, all four configuration combinations pass their executable contract tests, and readonly persistence plus lifecycle-only behavior pass natively on Windows. Missing macOS verification or bug checks do not block completion or release and remain tracked after release.

[Back to roadmap](../../../ROADMAP.md) · [Shared roadmap contracts](../index.md)
