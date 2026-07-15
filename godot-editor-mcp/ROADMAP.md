# Godot Editor MCP Roadmap

## Phase checklist

- [ ] Phase 7 — Make edited-scene and asset inspection targeted and complete.
- [ ] Phase 8 — Inspect the running scene through a read-only runtime probe.
- [ ] Phase 9 — Capture, drive, and validate gameplay with a minimal fixed API.
- [ ] Phase 10 — Author scenes through bounded atomic transactions.
- [ ] Phase 11 — Add autoload helpers and finish proven project-workflow gaps.

## Phase delivery contract

Each phase must leave the application working and releasable. A phase is
complete only when its workflow works end to end, security and failure
boundaries are covered, the complete existing suite and relevant Godot
integration checks pass, and all affected documentation and examples are
updated.

After each phase, increase the application version according to the repository
guidelines and keep the Python package, Godot plugin, runtime-reported versions,
README, history, examples, and this checklist consistent.

## Direction

The remaining roadmap prioritizes the shortest reliable LLM-to-Godot loop:

1. Read only the relevant editor state without wasting model context.
2. Observe the running game rather than assuming it matches the edited scene.
3. Exercise gameplay and verify the result.
4. Apply larger authoring changes atomically and safely.

The server must remain offline-capable, dependency-free at runtime, portable
across macOS, Linux, and Windows, and practical for small local models. New
tools should be added only when a focused existing tool cannot express the
workflow clearly. Prefer small schemas, targeted reads, bounded results,
explicit identities, and stable errors over editor API completeness.

## Current foundation

The existing implementation already provides structured bridge errors,
operation and event identities, run scoping, capability negotiation,
project-scoped discovery, bounded diagnostics, detailed editor/import state,
and Python-side waits. Future work should reuse those contracts rather than
introducing parallel lifecycle or error mechanisms.

## Phase 7 — Targeted inspection and stable pagination

Inspection result size is already a larger context cost than the tool schemas.
This phase makes current edited-scene and asset reads useful on real projects
before runtime inspection adds another potentially large data source.

### Targeted reads

- Extend edited `scene_tree` reads with an optional root, maximum depth, class
  filter, and result limit.
- Extend `node_info` with exact property-name and category filters. Preserve
  Godot property category metadata so filters remain predictable.
- Keep concise defaults and require explicit requests for broader results.
- Return `truncated`, snapshot identity, and continuation availability whenever
  a bounded result is incomplete; never silently present a prefix as complete.

### Pagination

- Add opaque continuation cursors to assets, the edited scene tree, and node
  properties.
- Bind each cursor to the normalized query and a snapshot identity:
  filesystem generation for assets, scene identity and UndoRedo version for
  edited trees, and node identity plus property-list fingerprint for properties.
- Authenticate stateless cursors with material derived from the project token,
  or use equivalently bounded server-side cursor IDs if that is measurably
  simpler.
- Reject tampered, expired, query-mismatched, and snapshot-mismatched cursors.
  Return `stale_cursor` when the underlying snapshot changes.
- Keep cursor contents opaque and never include token material or reusable
  object references.

### Completion gate

- Targeted reads materially reduce normal result size without losing category
  or path information needed for follow-up calls.
- Pagination has no duplicates or omissions for an unchanged snapshot.
- Scene edits, filesystem changes, property-list changes, and query changes
  invalidate only the relevant cursors.
- Result and cursor sizes remain within configured limits.

## Phase 8 — Read-only runtime inspection

The edited scene cannot reveal nodes spawned, removed, or changed by scripts.
This phase adds runtime observation without exposing runtime mutation or method
execution.

### Runtime data plane

- Add a tiny bundled runtime probe using Godot's public debugger channel and an
  editor-side debugger gateway.
- Register and remove the probe with the editor plugin without overwriting a
  conflicting autoload.
- Keep the probe inert when `EngineDebugger.is_active()` is false and verify
  that it does not affect normal runs or exports.
- Complete a per-run handshake containing run ID, debugger-session ID, probe
  version, supported commands, and effective limits.
- Reject messages for another run, debugger session, project, or incompatible
  probe version.
- Open no runtime network port and evaluate no client-provided GDScript.

### Model-facing inspection

- Extend `scene_tree` and `node_info` with
  `tree_scope: "edited" | "runtime"`; return the scope explicitly.
- Return bounded runtime node path, name, class, parent, script, source scene,
  groups, process mode, visibility, and selected properties.
- Encode identifiers from run, debugger session, and runtime object identity
  without exposing raw reusable object references.
- Reject stale identifiers after stop, replacement run, or debugger reconnect.
- Reuse Phase 7 targeting, filters, truncation metadata, and cursor semantics;
  bind runtime cursors to run ID and runtime-tree generation.
- Exclude the probe from ordinary runtime-tree results.
- Support one active debug session initially. Return an explicit error for
  absent, incompatible, or ambiguous sessions.

### Completion gate

- Edited and runtime results cannot be confused.
- Spawned nodes and script-driven property changes can be observed with bounded
  results.
- Stale runtime identifiers and cursors fail after stop and replacement run.
- Missing probe, incompatible probe, no active run, and multiple sessions
  produce stable errors.
- A focused executable spike validates debugger messaging before the complete
  feature is treated as feasible.

## Phase 9 — Minimal gameplay validation

This phase completes the observe-act-verify loop. It deliberately starts with
Input Map actions and a small declarative condition set instead of physical key
simulation or a general expression language.

### Game-view capture

- Add `capture_game_view` for the running project's main viewport.
- Enforce maximum source dimensions, output dimensions, pixel count, encoded
  bytes, and operation time.
- Stage captures only under
  `.godot/godot_mcp/captures/<operation-id>.png`.
- Return only capture identity and metadata over the editor bridge. Have Python
  derive the path, validate PNG signature and size, construct the MCP image
  result, and delete the staged file.
- Never accept an arbitrary runtime-provided filesystem path.

### Action input

- Add `send_input` for a validated Input Map action, pressed/released state,
  strength, and bounded duration or frame count.
- Require the active run ID and reject unknown actions, unbounded holds,
  excessive durations, and excessive concurrent inputs.
- Schedule release inside the runtime process so cleanup still occurs after an
  MCP or editor-bridge disconnect.
- Release all injected actions when the run ends or the probe shuts down.
- Mark injected input separately from physical user input in diagnostics.

### Minimal runtime conditions

- Add `wait_for_runtime_condition` with only:
  - play-state or run transition;
  - node exists or does not exist;
  - node-count comparison under a path or group;
  - bounded property equality or numeric/string comparison.
- Require explicit scope and run identity where applicable.
- Bound timeout, property value size, traversal depth, and returned evidence.
- Do not support scripts, expressions, method calls, regexes, arbitrary
  property traversal, signal subscriptions, or nested condition composition in
  this phase.

### Completion gate

- Captures contain the game viewport rather than editor chrome and cannot
  escape the staging directory.
- Input is released after success, timeout, client termination, and run stop.
- A request for one run cannot observe or affect another run.
- Every supported condition is deterministic, bounded, and unable to execute
  project code.
- Capture, input, renderer, and cleanup behavior are tested in live runtime
  fixtures, not only unit tests.

## Phase 10 — Core scene transaction engine

Current focused mutations remain useful for tiny mode, but larger scene changes
need one validated action, stable references between operations, and protection
against editing a scene that changed after inspection.

### Common value codec

- Move all model-facing JSON-to-Variant and Variant-to-JSON conversion into one
  bounded service.
- Support the types needed by common 2D, 3D, UI, script, and resource workflows:
  vectors, colors, rectangles, transforms, quaternion/basis values, enums,
  bit flags, scene-relative `NodePath`, and validated resource references.
- Add typed containers or packed primitive arrays only where a supported
  property workflow requires them.
- Use explicit tagged objects for resource and node references so strings are
  never reinterpreted implicitly.
- Validate resource class compatibility against property type and class hints.
- Bound nesting depth, element and key counts, string and packed-array lengths,
  total encoded size, and numeric validity; reject non-finite numbers.
- Report supported value forms through `capabilities`.

### Structural editing

- Support remove, rename, reparent, attach/detach script,
  connect/disconnect signal, and add/remove group membership.
- Preserve all state required for correct undo and redo.
- Return final node paths and path transitions affecting later operations.
- Support normal scene ownership and explicitly safe instantiated-scene cases.
  Reject inherited or editable-child mutations that cannot yet be proven safe.
- Reject unsafe root edits and invalid ownership changes before mutation.

### Atomic transactions

- Add bounded `scene_transaction` in `small` and `large` modes.
- Route `add_node`, `instantiate_scene`, and `set_property` through the same
  engine while retaining their focused tiny-mode schemas.
- Require optional optimistic preconditions for scene identity and UndoRedo
  version so a transaction can reject state changed since inspection.
- Validate every operation, reference, path transition, resource, signal, and
  ownership rule before creating an UndoRedo action.
- Allow transaction-local handles for nodes created or renamed earlier in the
  same batch.
- Commit the complete batch as one undo step, check postconditions, and undo
  immediately if an unexpected postcondition fails.
- Bound operation count, created nodes, tree depth, payload, and retained undo
  data. Return concise per-operation results and final dirty state.
- Build expanded scene creation—root script, groups, initial properties, and
  initial child tree—by translating it into the transaction representation.

### Completion gate

- Failed prevalidation leaves the scene unchanged and creates no undo action.
- Stale optimistic preconditions reject without mutation.
- A complete transaction is one undo step and survives undo, redo, save, close,
  and reopen.
- Core ownership behavior is verified for normal and explicitly supported
  instantiated-scene cases.
- Property type mismatches, tagged references, bounds, and rollback paths are
  covered by GDScript and headless editor tests.

## Phase 11 — Project workflow helpers

Add only project-level helpers that close common workflows not safely handled
by the existing focused settings tools.

### Deliverables

- Add `list_autoloads`.
- Add `autoload_patch` for add, update, and remove with optional expected-value
  comparison.
- Add `list_editor_plugins` as compact troubleshooting metadata; do not expose
  arbitrary plugin mutation.
- Use ProjectSettings and editor plugin APIs rather than editing
  `project.godot` text.
- Validate singleton names and `res://` script or scene paths, reject conflicts,
  protect the Godot MCP runtime probe, and report required reload level.
- Keep tool exposure in `small` and `large`; do not increase tiny-mode schema
  cost.
- Ensure `capabilities` reports all released runtime, transaction, value,
  cursor, condition, tool-mode, and effective-limit features accurately.

### Completion gate

- Autoload compare-and-swap behavior prevents overwriting changed state.
- Conflicting names, invalid paths, protected probe changes, and save failures
  return stable errors.
- Reload requirements integrate with Phase 3 end to end.
- Capability output matches the actual active mode, plugin version, runtime
  probe, and supported value/condition sets.

## Tool-mode policy

Keep modes nested and minimize fixed context cost:

- All modes receive reload, targeted inspection, pagination on existing reads,
  and the edited/runtime scope selector on existing scene inspection tools.
- `tiny` retains focused single-edit tools backed by the shared transaction
  engine.
- `small` and `large` receive capture, action input, runtime conditions,
  transactions, and project helpers.
- `large` retains desktop coordination through `select_node` and
  `start_editor`.
- Prefer adding an optional argument to the tool that starts or reads a workflow
  over introducing a general predicate, query, or editor-command tool.

## Deferred until demonstrated need

The following are intentionally outside the committed phases:

- Physical-key or OS-level input synthesis.
- Arbitrary runtime node mutation, method calls, expressions, or GDScript
  evaluation.
- Signal watches and nested `all`/`any` runtime-condition composition.
- Subviewport capture selection and simultaneous multiple-debug-session
  support.
- Exhaustive support for every Godot Variant or packed-array type.
- Duplicating arbitrary subtrees and editing complex inherited or editable-child
  ownership states.
- Enabling, disabling, installing, or removing arbitrary editor plugins.

Add one of these only after a concrete workflow demonstrates that existing
focused tools cannot solve it. Any addition must receive its own bounded schema,
capability flag, integration tests, documentation, and versioned phase or
roadmap revision.

## Security and resource constraints

Every phase must preserve existing confinement and add feature-specific bounds:

- Treat every MCP argument, bridge/runtime message, cursor, operation record,
  and staged artifact as untrusted.
- Keep the bridge on localhost with constant-time authentication and never
  weaken authentication during discovery or reconnect.
- Keep model-facing paths project-relative or `res://`, except for the existing
  editor-state project identity field.
- Bound event and diagnostic rings, operation history, tree and property
  results, cursors, watches, input duration, capture dimensions and bytes,
  transaction operations, retained undo data, and nested values.
- Never accept an arbitrary filesystem path from the runtime process.
- Never overwrite assets, scenes, resources, autoloads, or project settings
  implicitly.
- Keep runtime inspection read-only and the runtime probe inert outside an
  authenticated active debugger session.

## Test and validation strategy

Maintain dependency-free coverage at several levels:

1. Python unit tests for schemas, dispatch, errors, waiting, cursors, artifacts,
   discovery, reconnect, and result bounds.
2. End-to-end stdio tests for MCP initialization, tool listing, calls, mode
   enforcement, and stdout/stderr isolation.
3. Pure GDScript tests for codecs, validation, cursor state, conditions, and
   transaction planning.
4. Headless editor integration tests against temporary projects and dynamically
   allocated ports.
5. Runtime fixtures for spawned nodes, property changes, captures, action input,
   stale identities, and cleanup.
6. Subprocess tests for editor shutdown, restart, pending-operation recovery,
   and authenticated reconnection.
7. Manual native checks before changing platform or renderer compatibility
   claims.

Headless tests must not share the default port. They must configure only their
temporary project, wait for a matching discovery record, and clean up spawned
processes and temporary records. Run the complete Python suite and Godot 4.7
headless plugin load after every behavior change, plus focused live tests for
editor APIs, runtime messaging, imports, captures, input, reload, and UndoRedo
behavior.

## Documentation and release requirements

Every completed phase must update:

- `README.md` tool tables, setup, examples, limits, and platform notes.
- `HISTORY.md` with the released version and behavior changes.
- Configuration examples affected by modes, ports, probe registration, or
  environment requirements.
- This roadmap's checklist and detailed phase wording.
- The repository-level `AGENTS.md` when supported behavior, constraints,
  compatibility, or known issues change.

Release notes and pull requests must state context and memory impact,
dependencies, security implications, migration behavior, tests actually run,
and native platforms actually verified.
