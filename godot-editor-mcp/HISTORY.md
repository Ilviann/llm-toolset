# Godot Editor MCP history

This file records released changes. Planned work is tracked separately in
[`ROADMAP.md`](ROADMAP.md).

## 0.16.0 — 2026-07-15

- Added small/large-mode `list_autoloads` and atomic `autoload_patch` helpers
  with bounded add/update/remove batches, optional compare-and-swap values,
  dry runs, normalized diffs, and explicit project-reload requirements.
- Validated singleton identifiers and existing `res://` Node-script or scene
  paths, rejected class/engine/global-name conflicts, protected the Godot MCP
  runtime-probe name and path, and restored the complete batch after save
  failures.
- Added read-only `list_editor_plugins` troubleshooting metadata for bounded
  installed/enabled plugin records without exposing plugin mutation.
- Expanded capabilities with project-workflow features, supported autoload
  operations, exact runtime-condition vocabulary, and effective autoload/plugin
  limits while preserving the 12-tool tiny mode.
- Added focused Godot 4.7 workflow coverage and extended the native macOS
  editor/reload integration to verify autoload persistence across authenticated
  restart and reconnect.

## 0.15.0 — 2026-07-15

- Added small/large-mode `scene_transaction` with bounded operation, created-node,
  depth, payload, and retained-undo limits; optional scene/UndoRedo optimistic
  preconditions; transaction-local handles; complete shadow-scene prevalidation;
  one-step commit; postcondition checking; and immediate rollback on failure.
- Added atomic add/instantiate/property changes plus remove, rename, reparent,
  script attach/detach, persistent signal connect/disconnect, and persistent
  group add/remove operations with final paths and safe scene-ownership checks.
- Routed the existing tiny-mode add, instantiate, and property tools through the
  shared engine and expanded scene creation with scripts, groups, properties,
  and bounded initial child trees.
- Expanded the common model-facing value codec, shared by settings and scene
  workflows, with bounded 2D/3D math values, enums,
  flags, packed primitive arrays, tagged scene-relative NodePaths, and explicit
  tagged node/resource references with property-class compatibility checks.
- Added focused codec and undo/redo coverage plus native Godot 4.7 integration
  for stale/preflight rejection, tagged references, instantiated-scene safety,
  single-version transactions, save, close, reopen, and persisted signals/groups.

## 0.14.0 — 2026-07-15

- Added small/large-mode game viewport capture with fixed project-internal PNG
  staging, source/output/pixel/byte/time bounds, Python signature and dimension
  validation, MCP image content, immediate cleanup, and stale-capture cleanup.
- Added run-scoped Input Map action injection with bounded strength and duration
  or frame holds, concurrent-input limits, runtime-owned automatic release,
  shutdown cleanup, and explicitly marked injected-input diagnostics.
- Added a fixed, non-composable runtime condition API for play transitions,
  node existence, bounded node counts, and built-in scalar property comparisons,
  with explicit run/scope identity, bounded evidence, and stable timeouts.
- Expanded the debugger handshake to probe protocol 2 and added Python,
  headless runtime, renderer-failure, cleanup, schema/limit, and opt-in live
  editor/game integration coverage for the observe-act-verify workflow.

## 0.13.0 — 2026-07-15

- Added a debugger-only, read-only runtime probe and editor debugger gateway
  with per-run project, session, version, command, limit, and nonce handshake
  validation; the probe opens no port, evaluates no supplied code, and remains
  inert when Godot's engine debugger is inactive.
- Extended `scene_tree` and `node_info` with explicit edited/runtime scope while
  preserving edited defaults, and added targeted runtime traversal, rich node
  metadata, filtered live properties, opaque runtime identities, and shared
  bounded cursor behavior.
- Added deferred debugger responses to the localhost bridge, stable missing,
  ambiguous, incompatible, stale-identity, and stale-cursor failures, plus
  autoload conflict protection and bounded runtime request/response limits.
- Added a Godot probe test and a native macOS Godot 4.7 editor/game spike that
  observes a script-spawned node and property change, paginates results, and
  rejects identities and cursors after a replacement run.

## 0.12.0 — 2026-07-15

- Split edited-scene tree and property reads into a dedicated inspector while
  retaining the existing tool schemas, bridge commands, wire fields, defaults,
  bounds, ordering, snapshot identities, and cursor behavior.
- Reduced `scene_commands.gd` to UndoRedo-backed node/property mutations and
  selection, with inspection traversal, fingerprints, and cursor logic removed.
- Added focused service-boundary coverage, parser/load guards, and live bridge
  regression coverage for independently registered inspection and mutation
  handlers under the duplicate-safe router.

## 0.11.0 — 2026-07-15

- Added targeted edited-scene reads with root, depth, exact class, property,
  and property-category filters plus concise default page sizes.
- Added opaque, expiring, bounded server-side continuation cursors for assets,
  edited trees, and node properties. Cursors bind normalized queries to
  filesystem, scene/UndoRedo/structure, or node/property-list snapshots and
  return `stale_cursor` after relevant changes.
- Preserved property category and scene-relative path metadata on every page,
  added explicit snapshot/truncation/continuation fields and cursor capability
  limits, and kept cursor contents free of token or object-reference material.
- Added focused cursor validation and a native Godot 4.7 authenticated workflow
  covering pagination continuity, filters, stale snapshots, reload, and
  reconnect on macOS.

## 0.10.0 — 2026-07-15

- Split edited-scene, run, import/filesystem, and `project.godot` transitions
  into focused state trackers behind the wire-compatible `editor_state` facade.
- Routed asset-import and project-file-write callbacks directly to their narrow
  trackers while preserving shared event and operation registries.
- Added validated Python views for editor-state and reload-status payloads,
  centralized deadline/cancellation/diagnostic-settling behavior, and injected
  waiter construction outside tool dispatch.
- Added focused headless transition characterization plus malformed identity,
  stale reload, cancellation, quiet-period, and composition tests; passed the
  complete Python suite and Godot 4.7 headless Phase 2–6 checks on macOS.

## 0.9.0 — 2026-07-15

- Replaced the broad inherited Godot command base with focused project-path,
  scene-node, property-value, and input-event collaborators, and injected only
  the editor, undo, operation, import, and project-file callbacks each command
  service uses.
- Centralized cross-platform project identity and bounded atomic JSON records
  for discovery heartbeats and reload recovery while preserving existing wire
  fields, ownership checks, and crash-safe replacement behavior.
- Unified asset, launcher, validation, bridge, timeout, and cancellation
  failures under the structured Python domain-error boundary; unexpected
  programming errors now remain internal.
- Added platform-branch, record-bound, codec, dependency-surface, and error-
  boundary regression tests and passed the full Python and focused Godot 4.7
  headless suites on macOS.

## 0.8.0 — 2026-07-15

- Consolidated every model-facing schema, minimum mode, execution target,
  bridge route, project-path policy, and wait strategy into one typed tool
  registry while preserving tool schemas, ordering, modes, and wire behavior.
- Made Godot command services publish direct handler mappings and made command
  registration atomic and duplicate-safe with explicit ownership diagnostics.
- Added registry, schema-limit, error-code, live capability, and release-version
  contract checks, plus a focused headless-Godot router test.

## 0.7.0 — 2026-07-15

- Added the all-mode `reload_project` tool with explicit run-stop and scene-save
  safeguards, optional bounded waiting, and no silent scene discard.
- Added bounded atomic pending-reload records, startup validation and recovery,
  and distinct malformed, stale, version-mismatch, project-mismatch, save, and
  timeout failures.
- Added disconnect-aware project-scoped rediscovery, token reauthentication,
  bridge/project/operation identity verification, and cancellable waits.
- Added pure GDScript record checks, expanded the offline Python suite to 48
  tests, and passed a native macOS Godot 4.7 subprocess restart test covering
  active runs, dirty scenes, save failure, authenticated reconnect, and saved
  scene continuity.

## 0.6.0 — 2026-07-15

- Added a mutex-protected, 256-record diagnostic logger with stable event
  cursors, bounded fields and stacks, scope/severity/run filters, and the
  all-mode `get_diagnostics` tool.
- Expanded editor state with scene dirty tracking, filesystem phase/progress,
  per-resource import completion and errors, run diagnostic counts,
  `project.godot` hash state, reload requirements, and active operations.
- Added Python-side bounded waiting to scene open, scan/import, and run/stop
  actions, including monotonic deadlines, diagnostic quiet periods, and a
  configurable run startup health window.
- Added import lifecycle and timeout tests, raised the offline suite to 44
  tests, and verified plugin loading and the Phase 2 bridge surface in Godot
  4.7 stable on macOS.

## 0.5.0 — 2026-07-14

- Added bounded structured bridge errors, stable domain codes, and typed Python
  exceptions while retaining transitional support for legacy string errors.
- Added operation IDs for asynchronous scene, filesystem, and run requests;
  monotonic event IDs for observed state changes; and run-scoped stop requests.
- Added negotiated MCP protocol reporting through `capabilities`.
- Split Godot transport, routing, editor state, events, operations, and bridge
  discovery into focused services.
- Added an atomic project-hash-scoped bridge discovery heartbeat under `.godot`
  with no token or absolute project path, plus live, stale, malformed, and
  project-mismatch tests.

## 0.4.1 — 2026-07-13

- Split the Godot editor plugin into focused asset, scene, Project Settings,
  Input Map, shared-validation, and shared-limit modules while preserving the
  authenticated bridge protocol and command behavior.

## 0.4.0 — 2026-07-13

- Added `project_settings_get` and atomic, compare-and-swap
  `project_settings_patch` tools in `small` and `large` modes.
- Added `input_map_patch` for duplicate-free key, mouse, and joypad bindings.
- Added full-batch validation, dry runs, save rollback, reload requirements,
  and capability metadata for supported setting and input-event types.

## 0.3.2 — 2026-07-13

- Separated the static tool catalog, dispatch, stdio transport, and CLI
  composition from MCP request handling.
- Preserved the public `godot_editor_mcp.server` imports and tool behavior.
- Added end-to-end stdio tests for initialization, tool listing and calls,
  parse errors, and stdout/stderr separation.

## 0.3.1 — 2026-07-13

- Added portable macOS, Linux, and Windows setup documentation and process
  behavior.
- Made `start_editor` create a new session on POSIX and a detached process
  group on Windows.
- Kept macOS as the verified platform while documenting pending native Linux
  and Windows validation.

## 0.3.0 — 2026-07-13

- Added the large-mode-only `start_editor` tool.
- Restricted the launcher to the configured project and the absolute
  `GODOT_EXECUTABLE` path, with no model-provided arguments.
- Reported launcher configuration through `capabilities` and guarded repeated
  starts.

## 0.2.0 — 2026-07-13

- Added nested `tiny`, `small`, and `large` tool modes, defaulting to `tiny`,
  with dispatch-time enforcement.
- Added `capabilities` with MCP and bridge versions, active mode, exposed
  tools, supported bridge commands, Godot version, optional features, and
  effective limits.
- Made `scan_asset` public in `small` and `large` modes.
- Expanded `editor_state` with project and bridge identity, main scene,
  filesystem scan state and generation, and run metadata.
- Added run IDs and run-transition tracking to scene control and editor state.

## 0.1.1 — 2026-07-12

- Updated and verified the plugin for Godot 4.7 stable.
- Added Godot's generated GDScript UID file and updated compatibility
  documentation.

## 0.1.0 — 2026-07-12

- Added the dependency-free stdio MCP server and authenticated localhost Godot
  editor plugin.
- Added bounded editor-state, scene inspection, selection, property editing,
  and save/run/stop tools.
- Added asset discovery, asset information, staged imports, folder and
  whitelisted resource creation, scene creation/opening, node addition, and
  PackedScene instantiation.
- Added root confinement, traversal and symlink protections, protected-folder
  rules, request and file-size limits, and no-overwrite imports.
- Added offline unit tests and a minimal Godot plugin-validation project.
