# Godot Editor MCP Roadmap

## Phase checklist

- [x] Phase 1 — Establish protocol, lifecycle, and discovery foundations.
- [x] Phase 2 — Add diagnostics, import state, and bounded waits.
- [ ] Phase 3 — Reload and reconnect without losing project identity.
- [ ] Phase 4 — Inspect the running scene safely.
- [ ] Phase 5 — Capture, drive, and validate gameplay.
- [ ] Phase 6 — Author scenes through atomic transactions.
- [ ] Phase 7 — Add project helpers and stable pagination.

## Phase delivery contract

Each phase is a complete, releasable increment, not only a collection of code
changes. A phase may use foundations delivered by preceding phases, but it is
complete only when all of its scoped workflows work end to end, its failure and
security boundaries are covered, the full existing suite and relevant focused
integration checks pass, and every affected user or maintainer document is
updated. Documentation includes the README, examples, limits, platform notes,
history records, and this roadmap as applicable.

The completion gates below are phase-specific additions to this shared
contract. Testing, documentation, examples, and release verification must be
completed in the same phase as the feature; they must not be postponed to a
separate final phase. Experimental internals may be introduced for a later
phase only when they do not break, partially expose, or misdocument the working
feature set delivered by the current phase.

## Direction

The planned features are feasible, but they should not be implemented as
independent additions to the current command handlers. Diagnostics, waiting,
reload, runtime inspection, capture, and input all require shared state,
operation lifecycle, and error contracts. Adding them before those foundations
would duplicate state and make failure handling unreliable.

Phase 1 delivered the protocol and architecture foundation. Structured errors,
operation IDs, event cursors, run scoping, capability negotiation, and
project-scoped discovery are now the base for the higher-priority user-facing
features.

Development must remain offline-capable and dependency-free at runtime. The
implementation should continue to use the Python standard library, Godot APIs,
localhost transport, and the existing per-project authentication token.

## Current baseline

The project already has a strong compact foundation:

- The Python server separates the static tool catalog, dispatch, stdio,
  localhost bridge, asset management, and editor launching.
- The Godot plugin separates scene, asset, Project Settings, Input Map,
  validation, and limit responsibilities.
- Root confinement, traversal and symlink denial, request limits, tool modes,
  authentication, and basic UndoRedo integration are implemented.
- The current Python suite contains 44 passing tests.
- The plugin is verified against Godot 4.7 stable on macOS.

The main limitations affecting the roadmap are:

- The editor bridge processes commands synchronously in the editor main loop
  and sends one response per short-lived TCP connection. A wait loop inside the
  plugin would freeze the editor.
- The Python bridge uses short-lived connections and a fixed request timeout,
  with no operation or reconnection coordinator.
- Bridge errors are structured; local filesystem and launcher errors still use
  concise string results and can be migrated when their domains expand.
- Editor state includes only basic filesystem and run observations.
- Asset scans are project-wide and are not associated with per-resource
  completion or errors.
- Property conversion is shallow and does not consistently bound nested
  containers.
- Scene mutations do not yet model inherited-scene restrictions, ownership
  restoration, or complete transaction preconditions.
- Automated tests cover Python behavior and plugin loading, but not Godot-side
  command semantics, runtime behavior, or reload/reconnect workflows.

## Target architecture

### Python MCP process

Keep the MCP process small and synchronous at the stdio boundary, but introduce
focused internal services:

- `errors.py` — stable domain error codes, bounded details, and typed
  exceptions.
- `waiting.py` — monotonic deadlines, concise polling, startup health windows,
  reconnect handling, and cancellation on MCP shutdown.
- `captures.py` — safe lookup, validation, result construction, and cleanup for
  staged captures.
- Tool catalog modules grouped by core, runtime, authoring, and project
  workflows, composed into the existing nested modes.

Preserve `MCPServer`, mode/catalog constants, `run`, and `main` imports from
`godot_editor_mcp.server` for compatibility.

### Godot editor plugin

Refactor the main plugin into small services:

- `bridge_server.gd` — TCP lifecycle, authentication, framing, request size,
  and response size.
- `command_router.gd` — command registration and dispatch only.
- `editor_state_monitor.gd` — scene, filesystem, import, project-setting, and
  run observations.
- `event_store.gd` — bounded events with monotonic cursors.
- `diagnostic_store.gd` — thread-safe bounded diagnostic collection.
- `operation_registry.gd` — asynchronous operation metadata and completion.
- `debugger_gateway.gd` — runtime session ownership and messaging.
- `value_codec.gd` — all JSON-to-Variant decoding, result encoding, limits,
  and type compatibility.
- `scene_transaction.gd` — prevalidation, ownership planning, UndoRedo
  construction, postconditions, and rollback.
- `discovery_record.gd` — project-scoped bridge identity and heartbeat.

Existing feature handlers should depend on these services rather than
duplicating validation, error construction, or state tracking.

### Debug-runtime probe

Add a tiny bundled autoload that communicates through Godot's debugger channel.
It is the runtime data plane for inspection, capture, input, signal events, and
runtime diagnostics. The authenticated editor plugin remains the control plane.

The probe must:

- Be registered when the editor plugin is enabled and removed when the plugin
  is disabled.
- Refuse to overwrite an existing autoload with the same name but another path.
- Activate only when `EngineDebugger.is_active()` is true and remove or disable
  itself otherwise.
- Never open a network port or evaluate client-provided GDScript.
- Accept only a fixed, validated command set.
- Use a per-run handshake and reject messages for another run or debugger
  session.
- Remain negligible in memory and CPU use.

Using a bundled probe avoids depending on Godot's private remote-inspector wire
format, avoids OS-level input synthesis, and works for both embedded and
separate debug-game processes.

## Phase 1 — Protocol and architecture foundation

Refactor without intentionally changing existing tool behavior.

### Deliverables

- Replace bridge failures with an envelope containing `code`, `message`,
  bounded `details`, and `retryable`.
- Introduce stable codes for unauthorized, invalid argument, protected path,
  not found, editor busy, import pending, no active run, stale runtime ID,
  timeout, unsupported capability, stale cursor, and project mismatch.
- Make Python bridge exceptions preserve these fields through MCP tool errors.
- Add operation IDs for accepted asynchronous work and event IDs for observed
  state changes.
- Require run IDs on runtime-related requests and responses.
- Store the selected MCP protocol version when `initialize` succeeds and add it
  to `capabilities`.
- Split the main editor plugin while keeping existing command names and wire
  results compatible where possible.
- Write an atomic discovery record under `.godot` containing process identity,
  project-path hash, port, bridge/protocol versions, and heartbeat time.
- Never include the authentication token or an absolute project path in the
  discovery record.
- Never automatically connect to a record whose project hash does not match the
  configured project.

### Completion gate

- All current Python tests remain green.
- Old string-error bridge responses remain temporarily readable during an
  upgrade transition.
- Authentication, request limits, stdout isolation, and tool-mode enforcement
  remain unchanged.
- Discovery tests cover live, stale, malformed, and other-project records.

## Phase 2 — Diagnostics, editor state, imports, and bounded waiting

Completed in version 0.6.0. Godot 4.7 provides complete GDScript, scene-load,
editor, and engine/runtime logger coverage through the bounded store. Complete
C# compiler diagnostic capture remains build-dependent and is reported as an
unsupported language-specific capability.

### Diagnostics

Register bounded editor and runtime loggers. Logger callbacks may occur on
multiple threads, so callbacks must only normalize and append records under a
mutex. Model-facing reads must operate on a bounded snapshot.

Each record should contain:

- Event ID and monotonic or wall-clock timestamp.
- Severity and category.
- Bounded message text.
- Project-relative resource path when available.
- Line, column, function, and bounded stack frames when available.
- Run ID for runtime records; editor-only records use `null`.
- Origin marking for physical versus injected input when relevant.

`get_diagnostics` should support `scope` (`all`, `parser`, `runtime`, or
`editor`), `severity` (`error`, `warning`, or `all`), `since`, `limit`, and an
optional `run_id`. Reading diagnostics must not clear the store or mutate the
editor's Output or Debugger panels.

Guarantee GDScript, scene-load, editor, and engine/runtime diagnostics first.
Report language-specific capability flags if complete C# compiler diagnostic
capture cannot be provided by the installed Godot build.

### Editor state

Extend `editor_state` with:

- Edited-scene dirty state.
- Filesystem phase and normalized progress from zero to one.
- Active and recently completed per-resource imports.
- Import/load errors with bounded details.
- Current-run parser/runtime error and warning counts.
- Project-file hash state and `project_reload_required`.
- Active asynchronous operations in a concise bounded form.

Track dirty state using the active scene's UndoRedo history version together
with scene change and save signals. Validate this approach across Inspector
property edits, tree edits, undo back to the saved version, redo, scene
switching, embedded-resource edits, and MCP transactions. If Godot's public API
cannot observe a normal editor mutation accurately, isolate any necessary
Godot-4.7-specific compatibility code behind the state monitor rather than
inspecting editor UI controls throughout the plugin.

Detect external `project.godot` changes through a content hash. Plugin-owned
ProjectSettings saves must update a known-write marker so they are not reported
as unexplained external changes.

### Imports

Track filesystem progress and connect to reimport start/completion signals.
Associate requested imports with normalized `res://` paths and operation IDs.
Do not start another scan or reimport while Godot reports an active import.

An import is complete only when:

- The relevant scan/reimport operation ended.
- Godot reports a non-empty expected resource type where applicable.
- The resource is loadable, or a bounded per-resource error was recorded.

### Waiting

Add `wait` and `timeout_ms` to `open_scene`, `scan_asset`, `import_asset`, and
run/stop actions instead of adding a large general-purpose editor predicate
tool.

Waiting belongs in the Python process. It should poll concise state and event
commands with monotonic deadlines while allowing the Godot editor main loop to
remain responsive.

Awaited run results should return the new run ID and whether the process
survived a configurable startup health window. Diagnostics should be considered
settled after the relevant generation or operation has completed and no newer
record has appeared during a short bounded quiet period.

### Completion gate

- Parser errors, runtime errors, warnings, scene-load errors, and historical
  cursor separation are tested.
- Scene dirty state is correct after edit, undo, redo, save, and scene switch.
- Import success, import failure, already-running scan, and timeout are tested.
- Waiting never blocks the editor main thread.
- Diagnostic storage and result sizes remain within configured limits.

## Phase 3 — Safe project reload and reconnection

### Deliverables

- Add `reload_project` with `stop_running`, `save_scenes`, `wait`, and
  `timeout_ms`.
- Reject an active run unless `stop_running=true`.
- Reject dirty scenes unless `save_scenes=true`. Do not add silent discard
  behavior to the first version.
- Allocate and persist a pending reload operation before scheduling the editor
  restart.
- Send the bridge response before invoking the restart through a deferred call.
- On startup, validate the pending record, start the discovery heartbeat, and
  mark the operation complete.
- During a waited reload, tolerate temporary connection refusal, then
  reauthenticate and verify the project hash, bridge version, and operation ID.

### Completion gate

- Reload completes after the expected disconnect and reconnect.
- Active-run and dirty-scene safeguards work.
- Save failure prevents reload.
- Timeout, malformed operation record, stale record, and project mismatch
  return distinct structured errors.
- A bridge for another project is never used automatically.

## Phase 4 — Read-only runtime inspection

### Deliverables

- Register the runtime probe and editor debugger gateway.
- Complete a handshake containing run ID, debugger-session ID, probe version,
  supported commands, and effective runtime limits.
- Extend existing `scene_tree` and `node_info` inputs with
  `tree_scope: "edited" | "runtime"`.
- Return scope explicitly in every result.
- Return runtime node path, name, class, parent, script, source scene, groups,
  process mode, visibility, and a run-scoped identifier.
- Encode runtime identifiers from the run, debugger session, and runtime object
  identity without exposing a reusable raw object reference.
- Reject identifiers when the run or debugger session changes.
- Exclude the runtime probe from normal runtime-tree results.
- Keep runtime mutation disabled.
- Support one active debug session initially. If multiple run instances are
  active, return an explicit unsupported or ambiguous-session error instead of
  choosing one silently.

### Completion gate

- Edited and runtime results cannot be confused.
- Runtime paths and properties are bounded and encoded predictably.
- Stale IDs fail after stop and after a replacement run.
- Missing probe, incompatible probe, no active run, and multiple active sessions
  produce stable errors.

## Phase 5 — Gameplay validation

### Game-view capture

Add `capture_game_view`. Capture from inside the running project rather than
capturing editor chrome or the desktop.

- Select the main viewport first; add subviewport selection only with a bounded,
  unambiguous identifier.
- Enforce maximum source dimensions, output dimensions, pixels, encoded bytes,
  and operation time.
- Stage files only under
  `.godot/godot_mcp/captures/<operation-id>.png`.
- Return only the capture ID and metadata across the editor bridge.
- Have Python derive the staged path from the ID, validate the PNG signature and
  size, construct the MCP image result, and delete the staged file.
- Never accept a runtime-provided arbitrary path for Python to read.

### Bounded gameplay input

Add `send_input` with the following constraints:

- Support an Input Map action, pressed/released state, strength, and either a
  bounded duration or frame count.
- Optionally support a strictly validated physical key event.
- Require the active run ID.
- Reject unknown actions, unbounded holds, excessive durations, and excessive
  concurrent inputs.
- Schedule release inside the runtime process so release still occurs if the
  MCP process or editor bridge disconnects.
- Release all injected input when the run ends or probe shuts down.
- Record injected input separately from physical user input in diagnostics.

### Runtime conditions

Add `wait_for_runtime_condition` with a fixed declarative condition language:

- Node exists or does not exist.
- Node count comparison under a path or group.
- Bounded property equality or numeric/string comparison.
- Named signal observed after an event cursor.
- Play state or run transition.
- Bounded `all` and `any` composition if required after the single-condition
  form is stable.

Do not support scripts, expressions, method calls, regexes with uncontrolled
cost, or arbitrary property traversal. Bound condition depth, clauses, watched
signals, result size, and timeout.

### Completion gate

- Captures exclude editor chrome and cannot escape the staging directory.
- Input is released after success, timeout, client termination, and run stop.
- A request for one run cannot affect another run.
- Watches cannot execute methods or arbitrary GDScript.
- Signal event cursors separate historical and new observations.

## Phase 6 — Scene authoring transaction engine

### Value codec

Move all model-facing Variant conversion into one service and add safe support
for:

- `Vector4`, `Vector4i`, `Rect2`, and `Rect2i`.
- `Quaternion`, `Basis`, `Transform2D`, and `Transform3D`.
- `AABB`, `Plane`, and packed primitive arrays.
- Typed arrays and dictionaries.
- Validated resource references.
- Scene-relative `NodePath` values.
- Enum and bit-flag metadata.

Apply limits to depth, element count, keys, strings, packed-array length, total
encoded size, and numeric validity. Reject non-finite numbers.

Use explicit tagged objects for resource and node references so a normal string
cannot be misinterpreted. Validate resource class compatibility against the
target property's type and class hint before creating an UndoRedo action.

### Structural editing

Support removing, renaming, reparenting, duplicating, attaching/detaching
scripts, connecting/disconnecting signals, group membership, editable children,
and scene ownership.

For each operation:

- Reject edits to the root when the operation cannot safely target it.
- Preserve parent position, owner, script, groups, connections, and editable
  state for undo.
- Reject edits inside inherited or instantiated scenes unless Godot reports the
  target as editable and the requested ownership remains valid.
- Return the final node path and any path changes affecting later operations.

### Transactions

Expose a bounded `scene_transaction` tool in `small` and `large` modes. Keep
the existing focused `add_node`, `instantiate_scene`, and `set_property` tools
in `tiny`, but route them through the same transaction implementation.

The transaction engine must:

- Validate every operation, reference, path transition, resource, signal, and
  ownership rule before mutating the scene.
- Prepare new objects and capture undo state before creating the action.
- Commit the complete batch as one UndoRedo action with an explicit scene
  context.
- Check postconditions immediately after commit.
- Undo the just-created action if an unexpected postcondition fails.
- Bound operation count, created node count, tree depth, payload size, and
  retained undo data.
- Return per-operation results and final dirty state.

Allow transaction-local handles so later operations can refer to a node created
or renamed earlier without guessing its intermediate path.

### Expanded scene creation

Implement root script, root groups, initial properties, and initial child trees
by translating them into the same validated transaction representation used for
normal scene edits. Do not maintain a second scene-construction engine.

### Completion gate

- Every mutation is tested through do, undo, redo, save, close, and reopen.
- Ownership is verified for normal, instantiated, inherited, and editable-child
  cases.
- Failed prevalidation leaves the scene unchanged and creates no undo action.
- A complete transaction appears as one undo step.
- Property conversion edge cases and type mismatches are covered.

## Phase 7 — Project helpers, pagination, and final protocol coverage

### Autoloads and plugins

Add compact tools:

- `list_autoloads`.
- `autoload_patch` for add, update, and remove with optional expected-value
  comparison.
- `list_editor_plugins` for enabled plugin discovery.

Use `ProjectSettings` and editor plugin APIs rather than editing
`project.godot` text. Validate names and `res://` resource paths, protect the
Godot MCP runtime probe, reject conflicting singleton names, and report reload
requirements.

### Pagination and filters

Add authenticated opaque cursors:

- Assets: normalized query, filesystem generation, and traversal position.
- Edited scene tree: scene identity, UndoRedo version, and traversal position.
- Runtime scene tree: run ID, runtime tree generation, and traversal position.
- Node properties: node identity, filters, property-list fingerprint, and
  position.

Sign cursors with material derived from the existing project token. Reject
tampered, expired, query-mismatched, or generation-mismatched cursors. Return
`stale_cursor` when a snapshot changes instead of silently skipping or
duplicating results.

Add property-name and category filters to `node_info`. Preserve Godot's property
category markers while scanning metadata so filtering remains predictable.

### Completion gate

- Pagination produces no duplicates or omissions for an unchanged snapshot.
- Tree, property, filesystem, and run changes invalidate the correct cursors.
- Tampered cursors are rejected without exposing signing material.
- Capabilities report all supported features, value types, condition types,
  protocol versions, runtime-probe status, tool modes, and effective limits.

## Tool-mode policy

Keep modes nested and protect small-model context:

- All modes receive `get_diagnostics`, `reload_project`, complete
  `editor_state`, enhanced `scene_tree`/`node_info`, and optional waits on
  existing asynchronous tools.
- `small` and `large` receive capture, input, runtime-condition,
  `scene_transaction`, autoload, and plugin-discovery tools.
- `large` retains desktop coordination through `select_node` and
  `start_editor`.
- `tiny` retains the focused single-edit tools, backed internally by the same
  safe transaction engine.

Avoid a large general-purpose editor predicate schema. Optional waits on the
commands that initiate asynchronous work are shorter and easier for small
models to use correctly.

## Security and resource constraints

Every phase must preserve the repository's existing confinement rules and add
feature-specific limits:

- Treat every MCP argument, bridge message, runtime message, cursor, operation
  record, and staged artifact as untrusted.
- Keep the bridge bound to localhost and retain constant-time token checks.
- Never weaken authentication for discovery or reconnect behavior.
- Keep model-facing paths project-relative or `res://`, except for the existing
  editor-state project identity field.
- Bound diagnostic rings, event rings, operation history, stack frames, tree
  results, properties, import records, watch clauses, input duration, image
  dimensions, image bytes, transaction operations, and nested values.
- Do not evaluate arbitrary GDScript or expose a general runtime method call.
- Do not accept arbitrary filesystem paths from the runtime process.
- Do not implement unbounded input holds.
- Do not mutate runtime nodes through the read-only inspection interface.
- Do not overwrite autoloads, assets, scenes, or resources implicitly.
- Keep the runtime probe inert outside an active editor debugger session.

## Test and validation strategy

Maintain several dependency-free layers:

1. Python unit tests for schemas, dispatch, errors, waiting, cursors, discovery,
   artifacts, and reconnect behavior.
2. Existing end-to-end stdio tests for MCP initialization, tool listing, calls,
   and stdout/stderr isolation.
3. Pure GDScript tests for value conversion, structured errors, condition
   validation, transaction planning, and cursor state.
4. Headless editor integration tests against a temporary project and a
   dynamically allocated port.
5. Runtime fixture scenes for parser errors, runtime errors, signals, input,
   stale identifiers, and captures.
6. Subprocess tests for editor shutdown, restart, operation recovery, and
   authenticated reconnection.
7. Manual/native platform checks before changing compatibility claims.

The headless suite must not share the default port 6505. Tests should allocate a
free port, configure only their temporary project, wait for a matching discovery
record, and clean up the spawned editor process and temporary records.

Run the complete Python suite and the Godot 4.7 headless plugin load after every
behavior change. Run the focused live bridge/runtime suite for changes touching
editor APIs, runtime messaging, imports, captures, input, or reload.

## Major risks and decisions

### High risk

- Complete parser/runtime diagnostic coverage across script languages and
  editor states.
- Exact scene dirty-state observation through public Godot APIs.
- Runtime input cleanup during abnormal client or editor termination.
- UndoRedo correctness across inherited and instantiated scenes.

These require early executable spikes and must not be declared complete from
unit tests alone.

### Medium risk

- Reload response delivery before editor shutdown.
- Discovery heartbeat and stale-record behavior across platforms.
- Screenshot availability and color conversion across renderers and headless
  mode.
- Runtime debugger behavior with multiple run instances.
- Stable pagination while editor state changes.

### Initial decisions

- Prefer the public debugger channel plus a bundled runtime probe over Godot's
  private remote-inspector protocol.
- Support one active debug session in the first runtime release and reject
  ambiguity.
- Keep runtime inspection read-only.
- Perform waits in Python rather than blocking the editor plugin.
- Reject dirty-scene reloads unless saving is explicitly requested.
- Provide transactions only in `small` and `large`, while reusing their engine
  for focused `tiny` mutations.
- Treat structured errors and operation/run identity as foundational work.

## Documentation and release requirements

These requirements are part of every phase's completion gate. Each completed
phase must update:

- `README.md` tool tables, setup, examples, limits, and platform notes.
- `HISTORY.md` with the released version and behavior changes.
- Configuration examples affected by modes, ports, runtime-probe registration,
  or new environment requirements.
- This roadmap's checkboxes and any changed sequencing or risks.
- The repository-level `AGENTS.md` when supported behavior, constraints,
  compatibility, or known issues change.

Release notes and pull requests must describe context and memory impact,
dependencies, security implications, migration behavior, and the tests and
native platforms that were actually run.
