# Godot Editor MCP code structure, complexity, and coupling

## Scope and method

This document covers all hand-written Python and GDScript source under `godot-editor-mcp/`, including production code and tests. It excludes generated Godot `.uid` files, the CSV fixture, package and plugin metadata, project configuration, and prose documentation because they are not source code. Configuration files are still mentioned where code depends on their values or paths.

Dependencies below include:

- **direct dependencies**: Python imports, GDScript `preload`/`extends`, and explicitly injected collaborators;
- **runtime dependencies**: Python standard-library facilities or Godot editor APIs used by a file;
- **contract dependencies**: cross-process names and data shapes that must remain synchronized even though the files cannot import one another.

The implementation contains **48 production source files** (15 Python including the legacy root shim, and 33 GDScript), totaling about **9,633 lines**, and **22 test source files** totaling about **3,415 lines**. These line counts include comments and blank lines and are only structural indicators, not cyclomatic-complexity measurements.

## Implemented modules

The word *module* in this section means a cohesive architectural subsystem. The following list covers every production source file; the later file inventory maps each file back to one or more of these modules.

### M1. Python process entry and composition

**Responsibility:** Start the MCP process, parse options, construct the bridge, local asset guard, optional editor launcher, MCP server, and stdio transport. It also preserves legacy entry points.

**Files:** `godot-editor-mcp/server.py`, `godot_editor_mcp/__init__.py`, `godot_editor_mcp/__main__.py`, `godot_editor_mcp/cli.py`.

**Dependencies on other modules:** M2 for transport and request handling; M3 for mode and collaborator interfaces; M4 for local asset and editor-process services; M5 for the editor bridge; indirectly M6 because M2/M3 create operation waits.

### M2. MCP protocol presentation and stdio transport

**Responsibility:** Implement newline-delimited JSON-RPC over stdin/stdout, MCP initialization, protocol negotiation, `tools/list`, `tools/call`, text/image tool-result encoding, error presentation, and shutdown cancellation.

**Files:** `godot_editor_mcp/stdio.py`, `godot_editor_mcp/server.py`.

**Dependencies on other modules:** M3 for schemas, modes, and execution; M5 for typed domain errors; M1 only through lazy compatibility wrappers in `server.py`.

### M3. Tool API policy and dispatch

**Responsibility:** Define one typed specification per model-facing tool and derive schemas, stable mode subsets, bridge routes, project-path and wait policy, and expected live bridge contracts; dispatch those specifications through focused bridge or local handlers; validate and consume staged captures; enrich capabilities and coordinate an injected optional waiter.

**Files:** `godot_editor_mcp/tool_catalog.py`, `godot_editor_mcp/tool_dispatch.py`.

**Dependencies on other modules:** M4 for asset and launcher collaborators; M5 for bridge calls and bridge errors; M6 for asynchronous completion; M1 for the package version. Contract-coupled to M7, M9, M10, and M11 through command names, argument shapes, result shapes, mode capabilities, and version reporting.

### M4. Python local project and process services

**Responsibility:** Confine filesystem access to the Godot project/import root, validate paths and extensions, atomically copy bounded staged assets, create project folders, and optionally launch the configured Godot editor as a detached process.

**Files:** `godot_editor_mcp/assets.py`, `godot_editor_mcp/launcher.py`.

**Dependencies on other modules:** M5 because the launcher probes the bridge before starting another editor. M3 consumes both services through protocols. Asset import is contract-coupled to M10 because the dispatcher asks the plugin to scan the copied file.

### M5. Python bridge, discovery, and shared error model

**Responsibility:** Discover the live project-specific plugin port, authenticate with the project token, exchange bounded newline-delimited JSON on localhost, decode structured bridge failures into typed errors, and expose one bounded domain-error boundary shared by bridge, asset, launcher, validation, timeout, and cancellation failures.

**Files:** `godot_editor_mcp/bridge.py`, `godot_editor_mcp/discovery.py`, `godot_editor_mcp/errors.py`.

**Dependencies on other modules:** No higher-level Python module. Contract-coupled to M7 for transport/authentication, M8 for error envelopes and limits, M11 for reload reconnect validation, and the Godot side of discovery in M7.

### M6. Python asynchronous-operation waiting

**Responsibility:** Present validated typed views over state and reload payloads; poll those views for scene, import, run, stop, and project-reload completion; enforce shared deadlines and cancellation; require a brief diagnostic quiet period; validate run, operation, project, and bridge identities across reconnects.

**Files:** `godot_editor_mcp/state_payloads.py`, `godot_editor_mcp/waiting.py`.

**Dependencies on other modules:** M5 for bridge access and typed errors. Contract-coupled to M9 for the `state` representation and operation/import/run fields, to M10 for asset status, and to M11 for `reload_status` and persisted reload identities.

### M7. Godot plugin lifecycle, network bridge, discovery, and routing

**Responsibility:** Compose all editor-side services, register their direct callable maps with atomic duplicate-ownership checks, manage plugin/debugger-probe/autoload startup and shutdown, create/load the auth token, listen only on localhost, validate bounded requests, dispatch immediate or debugger-deferred responses, publish a project-specific heartbeat, and report capabilities.

**Files:** `plugin/addons/godot_mcp/godot_mcp.gd`, `bridge_server.gd`, `command_router.gd`, `discovery_record.gd`.

**Dependencies on other modules:** M8 for envelopes, limits, identities, cursors, and operation/event primitives; M9 for state and diagnostics; M10 for asset, scope-aware inspection, and scene mutation services; M11 for project settings, input map, autoload/plugin workflow, and reload commands; M12 for the debugger gateway and runtime probe. Contract-coupled to M3 and M5 through commands, transport, authentication, discovery records, limits, versions, and response envelopes.

### M8. Godot shared command infrastructure and bounded records

**Responsibility:** Provide narrow project-path, scene-node, common bounded scene-value, and input-event collaborators; opaque bounded cursor storage with query/snapshot validation; shared project identity and bounded atomic JSON records; success/failure envelopes and error codes; resource and transaction bounds; operation IDs; and bounded editor event history. The scene-value codec owns tagged node/resource references, property-class compatibility, common 2D/3D math forms, enums/flags, packed arrays, and recursive input/output bounds.

**Files:** `project_path_guard.gd`, `scene_node_access.gd`, `property_value_codec.gd`, `input_event_codec.gd`, `cursor_store.gd`, `project_identity.gd`, `atomic_json_record.gd`, `command_limits.gd`, `error_envelope.gd`, `operation_registry.gd`, `event_store.gd`.

**Dependencies on other modules:** `scene_node_access.gd` receives only an editor interface; the remaining helpers have no higher-level service dependency. This is the low-level Godot module on which M7 and M9-M12 depend. It is contract-coupled to M3/M5/M6 through project identity normalization, limits, error codes, operation IDs, and response shapes.

### M9. Godot editor state and diagnostics

**Responsibility:** Own scene, run, import/filesystem, and project-file transitions in focused trackers; aggregate them through a concise stable state facade; control save/run/stop; collect bounded parser/editor/runtime diagnostics.

**Files:** `editor_state_monitor.gd`, `scene_state_tracker.gd`, `run_state_tracker.gd`, `import_state_tracker.gd`, `project_file_state_tracker.gd`, `diagnostic_store.gd`.

**Dependencies on other modules:** M8 for envelopes, event IDs, operation IDs, and bounded error semantics. Tracker instances are composed and polled by M7; M10/M11 receive only tracker callbacks. Contract-coupled to M6 through the aggregated validated state fields.

### M10. Godot asset, edited-scene, and transaction services

**Responsibility:** List targeted paginated assets, request scans, create resources, open scenes, route explicit edited/runtime inspection scope through the read-only handlers, inspect targeted edited-scene pages locally, create bounded initial scene trees, and author edited scenes through a shadow-prevalidated atomic transaction engine. The engine owns optimistic scene/UndoRedo preconditions, transaction-local handles, structural/property/script/signal/group operations, safe ownership policy, one-step commit, postconditions, rollback, and retained-undo bounds; focused tiny-mode add/instantiate/set tools adapt into the same representation.

**Files:** `asset_commands.gd`, `edited_scene_inspector.gd`, `scene_commands.gd`, `scene_transaction.gd`.

**Dependencies on other modules:** M8 for narrow path/node/value/cursor collaborators, limits, error envelopes, and operation IDs; M9 through injected import-tracking, filesystem-generation, and scene-dirty callbacks; M12 through the injected runtime inspector. M7 registers these services. Contract-coupled to M3/M4/M6 through command and result shapes, transaction value/reference schemas, limits, error codes, and the copy-then-scan workflow.

### M11. Godot project settings, input map, and reload commands

**Responsibility:** Read and atomically patch bounded project settings, normalize and patch Input Map events, list and compare-and-swap guarded autoloads, report compact read-only editor-plugin metadata, track reload requirements, safely restart the editor, persist/recover a reload operation across restart, and report reload completion.

**Files:** `project_settings_commands.gd`, `input_map_commands.gd`, `project_workflow_commands.gd`, `reload_commands.gd`.

**Dependencies on other modules:** M8 for the common value and input-event codecs, project-path validation, shared project identity/records, limits, envelopes, and operation IDs; M9 only through an injected project-file-write callback. M7 injects the guarded EditorPlugin autoload APIs, registers all services, and polls reload. Contract-coupled to M3 and M6 through tool/command arguments, capability limits, and reload identity/status fields.

### M12. Godot runtime inspection and minimal gameplay validation

**Responsibility:** Register an editor debugger gateway, complete a project/run/session/version/command/limit/nonce handshake with a debugger-only runtime autoload, inspect the active scene, capture the main viewport, inject bounded Input Map actions with automatic release, and evaluate a fixed bounded condition grammar without a runtime network port, supplied-code evaluation, arbitrary mutation, or method calls; translate results into deferred bridge responses and shared cursor semantics where applicable.

**Files:** `runtime_debugger_gateway.gd`, `runtime_scene_inspector.gd`, `runtime_gameplay_commands.gd`, `runtime_probe.gd`.

**Dependencies on other modules:** M8 for errors, limits, project identity, value encoding, and cursor records; M9 through an injected current-run identity; M7 for debugger-plugin/autoload composition and deferred bridge completion. M10 consumes the runtime inspector through injection. Contract-coupled to M2-M5 through inspection/gameplay schemas, MCP image results, staged-capture validation, debugger commands, and result shapes.

### M13. Automated verification

**Responsibility:** Verify Python units and MCP integration plus focused headless-Godot behavior, including registry and release contracts, live capabilities, duplicate-safe routing, bridge security/bounds, discovery, filesystem confinement, modes and routing, capture consumption, typed payloads and waiting, launcher portability, diagnostics, focused state transitions, gameplay validation, guarded project workflows, and reload persistence/reconnect.

**Files:** all files under `godot-editor-mcp/tests/` ending in `.py` and `godot-editor-mcp/plugin/tests/` ending in `.gd`.

**Dependencies on other modules:** Directly tests M2-M6 and selected pieces of M8-M12. The subprocess integration test additionally launches Godot with the plugin from M7, covers Phase 7 pagination/filter/staleness through the Phase 8 service boundary, validates the Phase 9 editor/game debugger data plane and replacement-run staleness, exercises Phase 10 input/conditions/cleanup and headless capture rejection, verifies Phase 11 transaction prevalidation, ownership, references, persistence, and one-version commits, verifies Phase 12 autoload/plugin metadata and persistence, and completes reload across the Python/GDScript boundary.

## Complexity and coupling assessment

### Main complexity concentrations

| Area | Indicator | Assessment |
| --- | ---: | --- |
| `tool_catalog.py` | 868 lines | Large but mostly declarative tool specifications and JSON Schemas. It centralizes routing, modes, paths, waits, project workflows, inspection/gameplay/transaction/cursor limits, and live-contract expectations; control-flow complexity remains low while cross-language policy is intentionally concentrated. |
| `runtime_probe.gd` | 972 lines | Bundled debugger-only data plane: validates editor identities, inspects the live scene, captures the viewport, schedules injected-action release, polls bounded conditions, hashes runtime identities, and bounds response data. Phase 10 made it the main complexity concentration; splitting inspection, capture/input, and condition execution behind the unchanged autoload protocol is the clearest follow-up refactor. |
| `runtime_debugger_gateway.gd` | 464 lines | Owns editor debugger-session discovery, handshake validation, request identities, bounded pending/deferred responses, local play-state waits, timeouts, and stopped-session cleanup. Temporal and cross-process branching make it the main editor-side runtime risk surface. |
| `runtime_gameplay_commands.gd` | 63 lines | Narrow editor-facing validation/router for run-scoped capture, input, and conditions. It keeps gameplay bridge ownership out of the inspection adapter and delegates execution to the debugger gateway/probe. |
| `edited_scene_inspector.gd` | 306 lines | Routes explicit edited/runtime scope while retaining edited traversal, structural snapshots, categorized property fingerprints, and pagination without mutation. Runtime work is delegated to M12. |
| `scene_transaction.gd` | 720 lines | Main Phase 11 state transition owner: creates PackedScene validation snapshots, resolves paths/handles, enforces ownership and optimistic preconditions, stages twelve mutation kinds, constructs one UndoRedo action, checks postconditions, and rolls back unexpected results. Its size is justified by the atomic cross-operation invariant, but operation-specific validators are the clearest future split if this vocabulary expands. |
| `property_value_codec.gd` | 562 lines | Shared bounded scene-value boundary for scalar/container values, 2D/3D math forms, enums/flags, packed arrays, and explicit node/resource references. The branching follows Godot Variant types; keeping it isolated prevents conversion drift across settings, resource creation, inspection, runtime inspection, scene creation, and transactions. |
| `scene_commands.gd` | 231 lines | Thin focused-tool adapters plus bounded initial scene construction and selection. Existing add/instantiate/set calls delegate to the transaction engine; expanded file creation validates the full in-memory tree before saving. |
| `project_settings_commands.gd` | 318 lines | Recursive type conversion plus validation, compare-and-swap behavior, transactional rollback, and reload classification. High branching and data-shape complexity. |
| `project_workflow_commands.gd` | 430 lines | Bounded autoload discovery, batch prevalidation, CAS, conflict/path/probe protection, EditorPlugin-backed mutation, rollback, reload classification, and compact plugin metadata. It is cohesive around project configuration; split plugin metadata from autoload mutation if either vocabulary grows. |
| `reload_commands.gd` | 305 lines | Stateful restart/recovery protocol with persisted records, time validation, dirty-scene safeguards, deferred cleanup, and identity checks. Shared record/identity primitives now remove its filesystem duplication. |
| `waiting.py` / `state_payloads.py` | 327 / 181 lines | Wait algorithms now operate on validated views, enforce the lockstep plugin version, and share deadline, cancellation, polling, and diagnostic-settling mechanics. Reload reconnect remains the main temporal branch. |
| `asset_commands.gd` | 311 lines | Combines bounded paginated filesystem traversal, Godot resource introspection, creation, scanning, and operation tracking. Its dependencies are explicit narrow collaborators. |
| `cursor_store.gd` | 105 lines | Owns bounded opaque IDs, expiry/eviction, normalized-query fingerprints, preflight lookup for remote snapshots, snapshot validation, and stale-cursor classification across edited, asset, and runtime reads. |
| `godot_mcp.gd` | Composition root, 28 direct source preloads | Intentional highest fan-out. It constructs shared services, four state trackers, the editor debugger gateway, scope-aware inspectors, guarded project workflows, probe autoload lifecycle, and deferred bridge resolver explicitly. |
| `import_state_tracker.gd` / `run_state_tracker.gd` | 150 / 128 lines | The largest extracted editor-state temporal owners. Run state additionally supplies the active identity to the debugger gateway and accepts integral JSON run IDs without weakening stop scoping. |
| `editor_state_monitor.gd` | 72 lines | Thin stable facade that aggregates focused tracker dictionaries and routes save/run/stop without owning temporal transitions. |
| `errors.py` / `diagnostic_store.gd` | 259 / 224 lines | Broad shared contracts. They are not algorithmically dominant, but many modules rely on their stable bounded shapes and identifiers. |
| `tool_dispatch.py` | 334 lines, 6 direct internal imports | Main Python orchestration hub. It resolves registry policy into focused local or bridge execution, preflight validation, waits, capability enrichment, staged PNG confinement/validation/cleanup, and structured domain errors. |
| `input_event_codec.gd` / `input_map_commands.gd` | 133 / 135 lines | Input-event conversion is isolated from transactional Input Map mutation, reducing each file's responsibility and dependency surface. |

### Coupling hotspots and change risks

1. **The bridge protocol is intentionally duplicated across languages and guarded by tests.** `tool_catalog.py` owns Python policy, while `godot_mcp.gd`, `command_router.gd`, and command-service handler maps own editor registration. Live capability and invariant tests fail on command, protocol, limit, error-code, wait-field, or version drift.
2. **Errors are a mirrored contract.** `errors.py` and `error_envelope.gd` duplicate stable error-code strings and bounded detail behavior. Unknown plugin codes degrade to generic `BridgeError`, but omitted or mismatched known codes weaken typed handling.
3. **Versions and capabilities remain duplicated but are release-checked.** `godot_editor_mcp/__init__.py`, `godot_mcp.gd`, package/plugin metadata, capability responses, and reload records all carry version information. The current source versions are `0.16.0`; Python and plugin releases are deployed together and exact matches are required. The runtime probe has a separate protocol version (`2`) checked during its handshake.
4. **Limits are mirrored with explicit regression checks.** Python byte limits and schemas must match `command_limits.gd` and live capabilities. Changing only one side fails the contract suite before it can cause rejected requests, oversized responses, or misleading metadata.
5. **Discovery and reload records are cross-language persistent schemas.** `discovery.py` must match `discovery_record.gd`. `waiting.py` must match `reload_commands.gd`. Project-path normalization, hashes, timestamps, record versions, operation IDs, and bridge versions are all compatibility-sensitive.
6. **Temporal state is isolated behind one aggregation contract.** Scene, run, import, and project-file trackers own disjoint mutable fields. `editor_state_monitor.gd` merges their concise results, so cross-language wire changes remain centralized without making the facade a state hub.
7. **Narrow Godot collaborators trade inheritance coupling for explicit composition.** Path confinement, scene-node access, property values, and input events now change independently. `godot_mcp.gd` has higher construction fan-out, but command services receive only collaborators they call.
8. **Dependency injection contains Python test coupling.** `MCPServer`, `ToolDispatcher`, `OperationWaiter`, and `EditorLauncher` accept small structural interfaces, so tests use fakes without extra dependencies. This lowers concrete coupling even though orchestration fan-out remains high.
9. **No third-party runtime dependency is present in the source.** Python uses only the standard library and GDScript uses Godot APIs. This keeps deployment coupling low and supports offline operation.
10. **Pagination is deliberately server-stateful and bounded.** `cursor_store.gd` keeps at most 128 opaque 48-character IDs for two minutes. Each record binds a normalized query fingerprint, snapshot ID, and next offset; asset, tree, and property services own their distinct snapshot construction while sharing validation and error semantics.
11. **Runtime work adds an asynchronous debugger boundary without adding a game-side socket.** The localhost editor bridge retains a client only when a runtime handler returns the private deferred marker. The gateway accepts one active debugger session, bounds requests and time, and validates every response against project, run, session, probe version, and request identity before the bridge can release it.
12. **The runtime autoload is guarded and debugger-only.** `godot_mcp.gd` persists only its reserved matching autoload, refuses conflicts, and removes only the path it owns. `runtime_probe.gd` registers and processes only when `EngineDebugger` is active, exposes only inspection plus the fixed capture/input/condition grammar, and contains no networking, arbitrary mutation, method execution, expression, or supplied-script evaluation path.
13. **Capture crosses the filesystem boundary through one fixed staging contract.** The probe derives a random-ID PNG path under `.godot/godot_mcp/captures`; it never accepts a path. Python independently derives and confines the same path, rejects symlinks and mismatched metadata, checks PNG signature/IHDR bounds, emits MCP image content, and deletes the staged file. The probe prunes abandoned files after two minutes.
14. **Atomic scene edits use a complete isolated preflight.** The transaction engine packs the current unsaved edited tree and instantiates a shadow scene, maps shadow nodes back to stable live objects, applies all path transitions and handle bindings only to the shadow, and does not create an UndoRedo action until validation succeeds. Internal node references are preserved by the PackedScene snapshot rather than raw `Node.duplicate`, which avoids cross-tree object leakage.
15. **Undo history identity is scene-associated.** Transactions create actions with the edited root as their custom context, and scene state plus edited-scene snapshots resolve the manager's object history ID before reading its version. This keeps optimistic preconditions, dirty state, cursor snapshots, undo, and redo on the same history.
16. **Project workflow mutation is narrow and transactional.** Autoload changes are fully prevalidated against a virtual state, use injected `EditorPlugin` add/remove APIs, verify the resulting `ProjectSettings` state, and restore every affected entry after an application or save failure. The runtime probe name and path remain outside the mutation surface; editor plugins are metadata-only.

### Overall assessment

The codebase is moderately sized and intentionally split by boundary: MCP presentation, Python orchestration, local filesystem/process operations, localhost transport, and editor-side command services. Most files are cohesive. The primary maintenance risk is not raw file size but **cross-language contract coupling** and **temporal state** around imports, play/stop, diagnostics, discovery, and reload. Tests cover the major seams, including one subprocess integration path for reload, which is the right place to contain that risk.

## Source file inventory and dependencies

### Python production files

#### `godot-editor-mcp/server.py`

**Responsibility:** Legacy executable shim that invokes the packaged CLI.

**Internal source dependencies:** `godot_editor_mcp/cli.py`.

**Module dependencies:** M1. No direct standard-library dependency.

#### `godot-editor-mcp/godot_editor_mcp/__init__.py`

**Responsibility:** Define the package identity and authoritative Python package version (`0.16.0`).

**Internal source dependencies:** None.

**Module dependencies:** M1. Its version is consumed by M2/M3 and must match the Godot plugin version in M7.

#### `godot-editor-mcp/godot_editor_mcp/__main__.py`

**Responsibility:** Support `python -m godot_editor_mcp` by delegating to the CLI.

**Internal source dependencies:** `cli.py`.

**Module dependencies:** M1.

#### `godot-editor-mcp/godot_editor_mcp/cli.py`

**Responsibility:** Parse project, mode, port, and import-root arguments; construct all concrete Python services; start stdio serving; convert startup validation failures to CLI errors.

**Internal source dependencies:** `assets.py`, `bridge.py`, `launcher.py`, `server.py`, `stdio.py`, `tool_catalog.py`, `tool_dispatch.py`.

**Module dependencies:** M1 directly; M2-M5 through its composition role. Runtime dependencies: Python `argparse` and `os`.

#### `godot-editor-mcp/godot_editor_mcp/stdio.py`

**Responsibility:** Read one JSON-RPC object per input line, write protocol-safe compact JSON responses to stdout, encode validated text and PNG image tool content, send diagnostics only to stderr, and close the handler on shutdown.

**Internal source dependencies:** None; depends on a structural `RequestHandler` supplied by `server.py`.

**Module dependencies:** M2. Runtime dependencies: Python `json`, `sys`, and typing/text-stream protocols.

#### `godot-editor-mcp/godot_editor_mcp/server.py`

**Responsibility:** Negotiate MCP versions, validate JSON-RPC requests, expose mode-filtered tools, invoke the dispatcher, encode normal/tool errors, and cancel waits when closed; retain older public imports and entry points.

**Internal source dependencies:** `__init__.py`, `errors.py`, `stdio.py`, `tool_catalog.py`, `tool_dispatch.py`; lazy compatibility imports from `cli.py`.

**Module dependencies:** M2 primarily; M1, M3, and M5. Runtime dependency: Python typing.

#### `godot-editor-mcp/godot_editor_mcp/tool_catalog.py`

**Responsibility:** Hold one typed tool specification for each MCP tool and derive public schemas, strict nested modes, stable orders, bridge routes, path/wait policies, and expected plugin commands, protocol version, inspection/gameplay limits, and error codes.

**Internal source dependencies:** None.

**Module dependencies:** M3. Runtime dependencies: Python `dataclasses` and typing. Contract dependencies: `tool_dispatch.py` and all registered editor-side commands in M7/M9-M11.

#### `godot-editor-mcp/godot_editor_mcp/tool_dispatch.py`

**Responsibility:** Route MCP tools to local asset/launcher services or short bridge commands; perform project-path preflight checks; strip Python-only wait fields; coordinate the injected operation waiter; validate, encode, and delete fixed-path staged captures; enrich capability results.

**Internal source dependencies:** `__init__.py`, `errors.py`, `tool_catalog.py`, `waiting.py`.

**Module dependencies:** M3 directly; M1, M4-M6. Runtime dependency: Python typing protocols. Contract dependencies: command registrations in `godot_mcp.gd`/`command_router.gd` and response fields from M9-M11.

#### `godot-editor-mcp/godot_editor_mcp/assets.py`

**Responsibility:** Validate project/import roots, reject traversal/symlink/protected destinations, validate asset paths, create confined folders, and copy one bounded staged asset atomically without overwrite.

**Internal source dependencies:** `errors.py` (`AssetError`).

**Module dependencies:** M4. Runtime dependencies: Python `os`, `tempfile`, `pathlib`, and typing. Workflow dependency: `tool_dispatch.py` requests a Godot scan after successful writes.

#### `godot-editor-mcp/godot_editor_mcp/launcher.py`

**Responsibility:** Probe for an already-running editor, validate `GODOT_EXECUTABLE`, and start the configured project in a detached POSIX session or Windows process group.

**Internal source dependencies:** `bridge.py` (`GodotBridge`, `BridgeError`), `errors.py` (`LauncherError`).

**Module dependencies:** M4 and M5. Runtime dependencies: Python `os`, `pathlib`, `subprocess`, and typing.

#### `godot-editor-mcp/godot_editor_mcp/bridge.py`

**Responsibility:** Validate project/localhost configuration, read the auth token, discover the port, send bounded command requests over TCP, extend only bounded runtime-condition socket deadlines, read bounded responses, and decode bridge failures.

**Internal source dependencies:** `discovery.py`, `errors.py`.

**Module dependencies:** M5. Runtime dependencies: Python `json`, `socket`, `pathlib`, and typing. Contract dependencies: `bridge_server.gd`, `error_envelope.gd`, `command_limits.gd`, and `discovery_record.gd`.

#### `godot-editor-mcp/godot_editor_mcp/discovery.py`

**Responsibility:** Normalize/hash project paths, validate bounded discovery records, reject another project's record, assess heartbeat freshness, and select the live or fallback bridge port.

**Internal source dependencies:** `errors.py` (`ProjectMismatchError`).

**Module dependencies:** M5. Runtime dependencies: Python `dataclasses`, `hashlib`, `json`, `os`, `pathlib`, `time`, and typing. Contract dependency: `discovery_record.gd`.

#### `godot-editor-mcp/godot_editor_mcp/errors.py`

**Responsibility:** Define stable error codes and typed domain/bridge errors, bound untrusted detail payloads, and decode current or legacy plugin failure payloads.

**Internal source dependencies:** None.

**Module dependencies:** M5. Runtime dependencies: Python `collections.abc` and typing. Contract dependency: `error_envelope.gd` and editor-side callers that select its codes.

#### `godot-editor-mcp/godot_editor_mcp/waiting.py`

**Responsibility:** Implement shared monotonic deadlines, cancellation, state polling and diagnostic settling around focused open-scene, asset-import, run-startup, stop, and reload/reconnect completion predicates.

**Internal source dependencies:** `errors.py`, `state_payloads.py`; accepts a bridge-compatible collaborator structurally equivalent to `bridge.py`.

**Module dependencies:** M6 and M5. Runtime dependencies: Python `dataclasses`, `time`, `threading.Event`, and typing. Contract dependencies: the aggregated state trackers, `operation_registry.gd`, `asset_commands.gd`, and `reload_commands.gd`.

#### `godot-editor-mcp/godot_editor_mcp/state_payloads.py`

**Responsibility:** Provide immutable, tolerant-to-extra-fields views that validate every state, import, operation, run, diagnostic, project, bridge, and reload identity field when a wait predicate consumes it.

**Internal source dependencies:** `errors.py` (`InvalidResponseError`).

**Module dependencies:** M6 and M5. Runtime dependencies: Python `collections.abc`, `dataclasses`, and typing. Contract dependencies: the state facade and reload-status wire responses in M9/M11.

### Godot plugin production files

#### `godot-editor-mcp/plugin/addons/godot_mcp/godot_mcp.gd`

**Responsibility:** Main `EditorPlugin` composition root: create shared services, inject guarded autoload APIs, register every command, manage token/port/listener/discovery plus debugger gateway and guarded probe-autoload lifecycle, poll asynchronous runtime/play-state services, publish capabilities, and synchronize scene-save notifications.

**Internal source dependencies:** `asset_commands.gd`, `bridge_server.gd`, `command_router.gd`, `cursor_store.gd`, `discovery_record.gd`, `diagnostic_store.gd`, `edited_scene_inspector.gd`, `editor_state_monitor.gd`, `error_envelope.gd`, `event_store.gd`, `import_state_tracker.gd`, `input_event_codec.gd`, `input_map_commands.gd`, `command_limits.gd`, `operation_registry.gd`, `project_file_state_tracker.gd`, `project_identity.gd`, `project_path_guard.gd`, `project_settings_commands.gd`, `project_workflow_commands.gd`, `property_value_codec.gd`, `reload_commands.gd`, `run_state_tracker.gd`, `runtime_debugger_gateway.gd`, `runtime_scene_inspector.gd`, `scene_commands.gd`, `scene_node_access.gd`, `scene_state_tracker.gd`, `scene_transaction.gd`.

**Module dependencies:** M7 directly and M8-M12 through composition. Runtime dependencies: Godot `EditorPlugin`, `EditorInterface`, `EditorUndoRedoManager`, `EditorDebuggerPlugin`, `ProjectSettings`, `OS`, `Crypto`, and file APIs. Contract dependencies: Python M3/M5 and plugin metadata/version.

#### `godot-editor-mcp/plugin/addons/godot_mcp/bridge_server.gd`

**Responsibility:** Maintain the localhost TCP listener and clients, buffer one bounded newline-delimited request, authenticate the token in constant-time style, invoke the injected router, retain only private debugger-deferred responses for later resolution, bound responses, and disconnect.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; injected dispatch callable from `command_router.gd` and pending resolver from `runtime_debugger_gateway.gd`.

**Module dependencies:** M7, M8, and M12. Runtime dependencies: Godot `TCPServer`, `StreamPeerTCP`, `PackedByteArray`, and `JSON`. Contract dependency: Python `bridge.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/command_router.gd`

**Responsibility:** Atomically register direct callables with explicit owners, reject duplicate command ownership, report sorted commands, and dispatch a command or return a structured unknown-command failure.

**Internal source dependencies:** `error_envelope.gd`; receives all service instances from `godot_mcp.gd`.

**Module dependencies:** M7 and M8, with injected dependencies on M9-M11.

#### `godot-editor-mcp/plugin/addons/godot_mcp/cursor_store.gd`

**Responsibility:** Issue opaque random continuation IDs; retain at most 128 two-minute records; fingerprint normalized queries; expose an expected-snapshot/offset preflight for remote runtime reads; and distinguish malformed, expired, query-mismatched, and stale-snapshot cursors.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`.

**Module dependencies:** M8. Runtime dependencies: Godot `Crypto`, `JSON`, string hashing, and monotonic time APIs. Injected into M10 asset/edited-scene services and M12 runtime inspection.

#### `godot-editor-mcp/plugin/addons/godot_mcp/discovery_record.gd`

**Responsibility:** Publish a periodic bridge heartbeat record and remove it on shutdown only when the bounded current record still belongs to this process.

**Internal source dependencies:** `project_identity.gd`, `atomic_json_record.gd`.

**Module dependencies:** M7 and M8. Runtime dependencies: Godot `ProjectSettings`, `OS`, `Time`, and `DirAccess`. Contract dependency: Python `discovery.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/project_path_guard.gd`

**Responsibility:** Validate model-facing project paths, extensions, protected write destinations, traversal, and symbolic-link confinement.

**Internal source dependencies:** `error_envelope.gd`.

**Module dependencies:** M8. Runtime dependencies: Godot `DirAccess` and path/string APIs. Injected only into M10 services that accept project paths.

#### `godot-editor-mcp/plugin/addons/godot_mcp/scene_node_access.gd`

**Responsibility:** Validate scene-relative node paths and node names, confine lookup to the edited scene root, and return bounded command envelopes.

**Internal source dependencies:** `error_envelope.gd`; receives `EditorInterface`.

**Module dependencies:** M8. Runtime dependencies: Godot editor, `Node`, and `NodePath` APIs. Injected only into M10 asset/scene services that validate names or resolve edited nodes.

#### `godot-editor-mcp/plugin/addons/godot_mcp/property_value_codec.gd`

**Responsibility:** Own the common bounded model-facing scene value contract: validate recursive JSON safety and finite numbers; decode scalar/container, vector/color, rectangle/transform/quaternion/basis, enum/flags, packed-array, tagged NodePath, and explicit node/resource forms; enforce property object-class hints; and encode bounded tagged references/results.

**Internal source dependencies:** `error_envelope.gd`.

**Module dependencies:** M8. Runtime dependencies: Godot Variant, math, node-path, string-name, scene-tree, ResourceLoader, property-hint, JSON, and packed-array APIs. Injected into M10 resource creation, edited inspection, scene creation, and transactions plus M12 runtime inspection.

#### `godot-editor-mcp/plugin/addons/godot_mcp/input_event_codec.gd`

**Responsibility:** Decode bounded key, mouse, joypad-button, and joypad-motion mappings and normalize Godot input events for stable comparison and responses.

**Internal source dependencies:** `error_envelope.gd`.

**Module dependencies:** M8. Runtime dependencies: Godot input-event and OS key-code APIs. Injected only into M11 settings/input services.

#### `godot-editor-mcp/plugin/addons/godot_mcp/project_identity.gd`

**Responsibility:** Normalize project paths with explicit Windows/POSIX behavior and produce the shared SHA-256 project identity used by discovery and reload recovery.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependencies: Godot `ProjectSettings`, `OS`, and `HashingContext`. Contract dependency: Python `discovery.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/atomic_json_record.gd`

**Responsibility:** Read bounded JSON records and replace them atomically through same-directory temporary files with cleanup on rename failure.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependencies: Godot `FileAccess`, `DirAccess`, `ProjectSettings`, `OS`, and `JSON`. Consumed by M7 discovery and M11 reload recovery.

#### `godot-editor-mcp/plugin/addons/godot_mcp/command_limits.gd`

**Responsibility:** Centralize editor-side byte, traversal, result, cursor lifetime/count, settings, input, and diagnostic bounds.

**Internal source dependencies:** None.

**Module dependencies:** M8. Contract dependencies: Python `bridge.py`, `tool_catalog.py`, and capability reporting in `godot_mcp.gd`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/error_envelope.gd`

**Responsibility:** Define stable editor-side error codes and bounded success/failure envelopes, classify legacy message failures, and extract public messages.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependency: Godot Variant/container/string APIs. Contract dependency: Python `errors.py` and `bridge.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/operation_registry.gd`

**Responsibility:** Allocate process-scoped operation IDs, retain a bounded registry, mark operations complete by ID or kind, recover completed operations, and expose concise active/recent views.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependency: Godot `OS` and container APIs. Consumed by M9-M11; contract-coupled to Python `waiting.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/event_store.gd`

**Responsibility:** Allocate monotonic editor event IDs and retain a bounded timestamped event history.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependency: Godot `Time` and containers. Consumed by `editor_state_monitor.gd`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/editor_state_monitor.gd`

**Responsibility:** Preserve the concise state command as a stable facade by merging focused tracker results with project and shared identity fields; route save/run/stop actions to the owning scene or run tracker.

**Internal source dependencies:** `error_envelope.gd`; injected scene, run, import, project-file, event, operation, and diagnostic collaborators.

**Module dependencies:** M9 and M8. Runtime dependencies: `Engine` and `ProjectSettings`. Contract dependency: Python `state_payloads.py` and `waiting.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/scene_state_tracker.gd`

**Responsibility:** Own edited-scene identity, selection, dirty baselines, explicit and observed saves, scene-change events, and open-scene operation completion.

**Internal source dependencies:** `error_envelope.gd`; injected editor, undo, event, and operation collaborators.

**Module dependencies:** M9 and M8. Runtime dependencies: editor selection, `Node`, UndoRedo history, and file modification-time APIs.

#### `godot-editor-mcp/plugin/addons/godot_mcp/run_state_tracker.gd`

**Responsibility:** Own run IDs, play/stop transitions, operation completion, runtime diagnostic association, run events, last-exit status, run-scoped control safeguards, and the active-run identity callback consumed by runtime inspection.

**Internal source dependencies:** `error_envelope.gd`; injected editor, event, operation, and diagnostic collaborators.

**Module dependencies:** M9 and M8. Runtime dependencies: editor play-control APIs.

#### `godot-editor-mcp/plugin/addons/godot_mcp/import_state_tracker.gd`

**Responsibility:** Own filesystem signals and generation, expose that generation to paginated asset reads, track scan transitions, pending/recent imports, import completion and failures, diagnostic association, filesystem events, and scan operation completion.

**Internal source dependencies:** None; receives editor, event, operation, and diagnostic collaborators.

**Module dependencies:** M9 and M8. Runtime dependencies: editor resource-filesystem signals, `ResourceLoader`, and bounded container APIs.

#### `godot-editor-mcp/plugin/addons/godot_mcp/project_file_state_tracker.gd`

**Responsibility:** Own `project.godot` content hashing, known-write baselines, periodic drift detection, and sticky reload-required state; accept an optional hash reader for deterministic transition tests.

**Internal source dependencies:** None.

**Module dependencies:** M9. Runtime dependencies: `FileAccess`, `HashingContext`, and `Time`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/diagnostic_store.gd`

**Responsibility:** Act as a thread-safe Godot logger, sanitize and categorize bounded parser/editor/runtime records, associate runtime records with run IDs, support cursor/filter reads, and supply counts/import errors.

**Internal source dependencies:** `error_envelope.gd`.

**Module dependencies:** M9 and M8. Runtime dependencies: Godot `Logger`, `Mutex`, `ScriptBacktrace`, `ProjectSettings`, `Time`, and error types. Consumed by `godot_mcp.gd` and `editor_state_monitor.gd`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/asset_commands.gd`

**Responsibility:** List filtered paginated assets with filesystem-generation snapshots, inspect one asset and dependencies, request scans, create whitelisted resources, and open packed scenes. Scene file creation now belongs to the transaction-facing scene service.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives editor and operation services plus import-tracking, filesystem-generation, project-path, scene-node, property-value, and cursor collaborators.

**Module dependencies:** M10, M8, and M9. Runtime dependencies: Godot editor resource filesystem, `DirAccess`, `FileAccess`, `ResourceLoader`, `ResourceSaver`, `ClassDB`, `PackedScene`, and resource APIs. Contract dependencies: Python `tool_dispatch.py`, `assets.py`, and `waiting.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/edited_scene_inspector.gd`

**Responsibility:** Publish the existing `tree` and `inspect` handlers, validate explicit edited/runtime scope, delegate runtime requests to M12, and return root/depth/class-targeted edited pages or exact-name/category-filtered edited properties with normalized paths, stable snapshots, truncation, and continuation. It performs no mutation.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives editor/undo-version services plus scene-node, property-value, shared cursor, and runtime-inspector collaborators.

**Module dependencies:** M10, M8, and M12. Runtime dependencies: Godot scene tree, property metadata, hashing/JSON, and editor undo-version APIs. Contract dependency: Python `tool_catalog.py`/`tool_dispatch.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/scene_commands.gd`

**Responsibility:** Preserve the compact add/instantiate/set/select command contracts by translating focused mutations into the shared transaction engine; create bounded initial scene files with optional scripts, groups, properties, and recursive built-in child trees; and select editor nodes. It owns no inspection traversal, fingerprints, cursors, or atomic batch mechanics.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives editor plus project-path, scene-node, property-value, and transaction collaborators.

**Module dependencies:** M10 and M8. Runtime dependencies: Godot scene tree, `ClassDB`, `ResourceLoader`, `ResourceSaver`, `PackedScene`, selection, property metadata, and group/script APIs. Contract dependency: Python `tool_catalog.py`/`tool_dispatch.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/scene_transaction.gd`

**Responsibility:** Implement bounded atomic edited-scene transactions: validate scene/UndoRedo optimistic preconditions, construct an isolated PackedScene shadow, map shadow nodes to stable live objects, resolve current paths and transaction-local handles, stage property and structural changes, enforce ownership/root/inherited-scene rules and retained-undo limits, commit one scene-associated UndoRedo action, return final paths and dirty state, and undo immediately when path postconditions fail. It also provides the operation vocabulary used by focused mutation adapters.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives editor, UndoRedo manager, project-path, property-value, and scene-state collaborators.

**Module dependencies:** M10, M8, and M9. Runtime dependencies: `PackedScene`, `ClassDB`, `ResourceLoader`, Node ownership/tree/signal/group/script/property APIs, `EditorUndoRedoManager`, JSON, and object instance identities. Contract dependency: Python `tool_catalog.py`/`tool_dispatch.py` and capability limits/error codes in M3/M7/M8.

#### `godot-editor-mcp/plugin/addons/godot_mcp/runtime_debugger_gateway.gd`

**Responsibility:** Discover active editor debugger sessions, enforce the single-session rule, validate probe hello/handshake identities and capabilities, send bounded runtime requests, evaluate editor-owned play-state waits, validate returned run/session/project/version/request identities, and retain bounded deferred responses until the localhost bridge retrieves them or they time out.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives the current-run callback and optional session/sender/clock collaborators.

**Module dependencies:** M12, M8, M9, and M7. Runtime dependencies: Godot `EditorDebuggerPlugin`, `EditorDebuggerSession`, `Crypto`, and monotonic time. Contract dependencies: `runtime_probe.gd`, `bridge_server.gd`, and Python bridge error handling.

#### `godot-editor-mcp/plugin/addons/godot_mcp/runtime_scene_inspector.gd`

**Responsibility:** Normalize runtime tree/property queries, preflight shared opaque cursors before remote work, attach expected snapshots and offsets, submit debugger requests, validate bounded result shape, and issue the next shared cursor without exposing debugger message details to the model-facing handlers.

**Internal source dependencies:** `error_envelope.gd`; receives `runtime_debugger_gateway.gd` and `cursor_store.gd` collaborators.

**Module dependencies:** M12, M8, and M7. Runtime dependencies: Godot dictionaries, arrays, callables, and typed debugger-plugin references. Contract dependency: Python `tool_catalog.py` scope/cursor schemas.

#### `godot-editor-mcp/plugin/addons/godot_mcp/runtime_probe.gd`

**Responsibility:** Act as the guarded runtime autoload, remain inactive without `EngineDebugger`, retry and complete the project/run/session/version/command/limit/nonce handshake, inspect the runtime tree/properties, capture the main viewport to a fixed staging folder, inject bounded Input Map actions with automatic release, poll the fixed condition grammar, hash runtime object identities, and advance structural generations without opening a network port, arbitrary mutation, method calls, or supplied-code evaluation.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`, `project_identity.gd`, `property_value_codec.gd`.

**Module dependencies:** M12 and M8. Runtime dependencies: Godot `Node`, `EngineDebugger`, `SceneTree`, property metadata, `Crypto`, `HashingContext`, `JSON`, and visibility/group/script APIs. Contract dependencies: `runtime_debugger_gateway.gd` and `runtime_scene_inspector.gd`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/runtime_gameplay_commands.gd`

**Responsibility:** Own the editor-facing `capture_game_view`, `send_input`, and `wait_runtime_condition` handlers; require the active run identity and explicit runtime scope; route play-state transitions locally through the gateway and all game-side work through the validated probe protocol.

**Internal source dependencies:** `error_envelope.gd`; receives the runtime debugger gateway and current-run callback.

**Module dependencies:** M12, M8, M9, and M7. Contract dependencies: Python gameplay tool schemas/dispatch and runtime probe commands.

#### `godot-editor-mcp/plugin/addons/godot_mcp/project_settings_commands.gd`

**Responsibility:** Read individual/prefixed settings, block secret/internal/input keys, delegate model-facing Variant conversion to the common bounded value codec, normalize the remaining InputEvent records, apply compare-and-swap patches transactionally, roll back failed saves, and classify reload requirements.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives the common property-value codec, input-event codec, and a project-file-write callback.

**Module dependencies:** M11, M8, and M9. Runtime dependencies: Godot `ProjectSettings`, Variant/type APIs, and input-event encoding. Contract dependency: Python tool schemas/dispatch in M3.

#### `godot-editor-mcp/plugin/addons/godot_mcp/input_map_commands.gd`

**Responsibility:** Validate and normalize key/mouse/joypad events, add/remove bindings without duplicates, change deadzones, update `InputMap`, save transactionally, and roll back failures.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives the input-event codec and a project-file-write callback.

**Module dependencies:** M11, M8, and M9. Runtime dependencies: Godot `ProjectSettings`, `InputMap`, `InputEventKey`, `InputEventMouseButton`, `InputEventJoypadButton`, `InputEventJoypadMotion`, and OS key-code APIs. Contract dependency: Python tool schema/dispatch in M3.

#### `godot-editor-mcp/plugin/addons/godot_mcp/project_workflow_commands.gd`

**Responsibility:** List bounded normalized autoloads, prevalidate compare-and-swap add/update/remove batches, reject invalid/conflicting names and unsafe/non-Node paths, protect the runtime probe, apply through injected editor autoload APIs, verify and roll back failed application/save attempts, classify project-reload requirements, and list compact read-only installed/enabled editor-plugin metadata.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; receives `project_path_guard.gd`, injected `EditorPlugin` add/remove callables, a project-file-write callback, and an injectable settings-save callable for deterministic failure coverage.

**Module dependencies:** M11, M8, M9, and M7. Runtime dependencies: Godot `ProjectSettings`, `ResourceLoader`, `ResourceUID`, `ClassDB`, `Engine`, `DirAccess`, `FileAccess`, `ConfigFile`, `Script`, and `PackedScene`. Contract dependency: Python tool schemas/capability limits in M3.

#### `godot-editor-mcp/plugin/addons/godot_mcp/reload_commands.gd`

**Responsibility:** Enforce run/dirty-scene safeguards, persist a bounded pending reload record atomically, defer editor restart until after response delivery, recover/validate completion after restart, report status, and defer cleanup until completion is returned.

**Internal source dependencies:** `error_envelope.gd`, `project_identity.gd`, `atomic_json_record.gd`; injected `operation_registry.gd`.

**Module dependencies:** M11 and M8; polled by M7. Runtime dependencies: Godot editor restart/save APIs, `ProjectSettings`, `OS`, `Time`, and `DirAccess`. Contract dependency: Python `waiting.py`.

### Python test files

#### `godot-editor-mcp/tests/test_assets.py`

**Responsibility:** Verify allowed imports, disabled import roots, size bounds, no-overwrite behavior, confined folder creation, traversal/symlink/protected-path rejection, and scene path validation.

**Internal source dependencies:** `godot_editor_mcp/assets.py`.

**Module dependencies:** M13 testing M4. Runtime dependencies: Python `os`, `tempfile`, `unittest`, and `pathlib`.

#### `godot-editor-mcp/tests/test_bridge.py`

**Responsibility:** Verify authenticated request encoding, live discovery port selection, safe legacy/structured error decoding, typed not-found errors, and project validation with fake sockets.

**Internal source dependencies:** `bridge.py`, `discovery.py`, `errors.py`.

**Module dependencies:** M13 testing M5. Runtime dependencies: Python `json`, `tempfile`, `time`, `unittest`, `pathlib`, and mocking.

#### `godot-editor-mcp/tests/test_contracts.py`

**Responsibility:** Verify unique registry names, stable public/catalog ordering, nested modes, complete local and bridge routes, schema-aligned path/wait/cursor policy and limits, mirrored editor error codes, live capability comparison behavior, and dependency-free release version consistency.

**Internal source dependencies:** `__init__.py`, `bridge.py`, `errors.py`, `tool_catalog.py`; reads package/plugin/runtime metadata plus `HISTORY.md` and `ROADMAP.md` as release records.

**Module dependencies:** M13 testing M1, M3, M5, and cross-language contracts with M7/M8. Runtime dependencies: Python `re`, `unittest`, and `pathlib`.

#### `godot-editor-mcp/tests/test_discovery.py`

**Responsibility:** Verify live, stale, malformed, and cross-project discovery-record handling plus explicit POSIX and Windows project-identity normalization branches.

**Internal source dependencies:** `discovery.py`, `errors.py`.

**Module dependencies:** M13 testing M5. Runtime dependencies: Python `json`, `tempfile`, `time`, `unittest`, and `pathlib`.

#### `godot-editor-mcp/tests/test_launcher.py`

**Responsibility:** Verify missing/relative executable failures, POSIX and Windows detach flags, safe process-start errors, and avoidance of duplicate editor starts.

**Internal source dependencies:** `bridge.py`, `launcher.py`.

**Module dependencies:** M13 testing M4/M5. Runtime dependencies: Python `tempfile`, `unittest`, `pathlib`, and mocking.

#### `godot-editor-mcp/tests/test_reload_integration.py`

**Responsibility:** Optional subprocess integration test that copies the plugin into a temporary project, launches Godot headlessly, verifies the live capability contract and Phase 12 autoload/plugin metadata, builds and persists an expanded scene, exercises a thirteen-operation atomic scene transaction with handles/tagged references/signals/groups/instantiation, rejects stale/preflight/unsafe editable-child requests without mutation, verifies one-version commit plus save/close/reopen persistence, exercises targeted asset/edited-scene pagination, completes a real editor/game debugger handshake, observes a script-spawned runtime node/property, injects and releases an action, evaluates runtime/play-transition conditions, verifies headless capture rejection, rejects replaced-run IDs/cursors, then persists a guarded autoload through reload safeguards and authenticated reconnect.

**Internal source dependencies:** `bridge.py`, `discovery.py`, `errors.py`, `tool_dispatch.py`; runtime dependency on the complete `plugin/addons/godot_mcp/` source and `plugin/project.godot` fixture.

**Module dependencies:** M13 testing M3, M5-M12 across the process boundary. Runtime dependencies: Python `os`, `shutil`, `signal`, `socket`, `subprocess`, `sys`, `tempfile`, `time`, `unittest`, and `pathlib`, plus an installed Godot executable.

#### `godot-editor-mcp/tests/test_server.py`

**Responsibility:** Verify MCP initialization/tool listing, nested mode subsets, forbidden-tool rejection, command mapping including runtime scope/identity forwarding, diagnostics availability, local-only wait fields, reload waits, import/scan flow, path validation, settings routing, capabilities, launcher exposure, structured local/bridge error encoding, internal programming-error propagation, and notification behavior using fakes.

**Internal source dependencies:** `bridge.py`, `errors.py`, `server.py` (which transitively exercises `tool_catalog.py`, `tool_dispatch.py`, `waiting.py`, and `stdio.py`).

**Module dependencies:** M13 testing M2-M6. Runtime dependencies: Python `json` and `unittest`.

#### `godot-editor-mcp/tests/test_stdio.py`

**Responsibility:** Verify end-to-end newline-delimited initialize/list/call output and ensure parse/internal diagnostics do not corrupt stdout.

**Internal source dependencies:** `server.py`, `stdio.py`.

**Module dependencies:** M13 testing M2 and indirectly M3. Runtime dependencies: Python `io.StringIO`, `json`, and `unittest`.

#### `godot-editor-mcp/tests/test_waiting.py`

**Responsibility:** Verify wait option validation, argument stripping, scene/import/run/stop completion, startup failure observation, timeout/cancellation, and reload reconnect/stale project/version/operation handling with deterministic fake clocks and bridges.

**Internal source dependencies:** `errors.py`, `waiting.py`.

**Module dependencies:** M13 testing M5/M6. Runtime dependency: Python `unittest`.

#### `godot-editor-mcp/tests/test_state_payloads.py`

**Responsibility:** Verify lazy field validation, extra-field preservation, malformed operation/import identities, complete reload identities, and inconsistent reload status rejection.

**Internal source dependencies:** `errors.py`, `state_payloads.py`.

**Module dependencies:** M13 testing M5/M6. Runtime dependency: Python `unittest`.

### GDScript test files

#### `godot-editor-mcp/plugin/tests/phase2_diagnostics_test.gd`

**Responsibility:** Headless Godot smoke test for diagnostic filtering, cursor behavior, bounded records, and stale cursor errors.

**Internal source dependencies:** `diagnostic_store.gd` (which transitively preloads `error_envelope.gd`).

**Module dependencies:** M13 testing M8/M9. Runtime dependencies: Godot `SceneTree` and logging APIs.

#### `godot-editor-mcp/plugin/tests/phase3_reload_record_test.gd`

**Responsibility:** Headless Godot unit test for pending reload-record validation, including malformed, stale, project-mismatch, and bridge-version-mismatch cases.

**Internal source dependencies:** `error_envelope.gd`, `reload_commands.gd`.

**Module dependencies:** M13 testing M8/M11. Runtime dependency: Godot `SceneTree`, time, and hashing/project APIs used by the tested script.

#### `godot-editor-mcp/plugin/tests/phase4_command_router_test.gd`

**Responsibility:** Headless Godot unit test for direct callable dispatch, stable command reporting, atomic registration, duplicate ownership rejection, and structured unknown-command failures.

**Internal source dependencies:** `command_router.gd` (which preloads `error_envelope.gd`).

**Module dependencies:** M13 testing M7/M8. Runtime dependency: Godot `SceneTree` and callable/container APIs.

#### `godot-editor-mcp/plugin/tests/phase5_infrastructure_test.gd`

**Responsibility:** Headless Godot characterization test for POSIX/Windows project identity, bounded atomic record replacement, property/input codecs, path confinement, and parser-level loading of every narrowed command service.

**Internal source dependencies:** all Phase 5 M8 helpers plus `asset_commands.gd`, `edited_scene_inspector.gd`, `scene_commands.gd`, `project_settings_commands.gd`, and `input_map_commands.gd`.

**Module dependencies:** M13 testing M8-M11. Runtime dependencies: Godot `SceneTree`, user-data file APIs, hashing, input events, and variant conversion APIs.

#### `godot-editor-mcp/plugin/tests/phase6_state_trackers_test.gd`

**Responsibility:** Characterize scene dirty/save/change transitions, run startup/stop, successful and failed imports, filesystem generations, project-file drift, operation completion, emitted event identities, and the exact concise aggregated state field set.

**Internal source dependencies:** `editor_state_monitor.gd`, all four state trackers, `event_store.gd`, and `operation_registry.gd`.

**Module dependencies:** M13 testing M8/M9. Runtime dependencies: Godot `SceneTree`, `Node`, signals, callables, and lightweight fake editor collaborators.

#### `godot-editor-mcp/plugin/tests/phase7_cursor_store_test.gd`

**Responsibility:** Verify opaque cursor sizing, unchanged-snapshot continuation, normalized-query mismatch rejection, stale-snapshot classification, expiry, and tamper rejection with a deterministic clock.

**Internal source dependencies:** `cursor_store.gd` (which transitively preloads `error_envelope.gd` and `command_limits.gd`).

**Module dependencies:** M13 testing M8. Runtime dependencies: Godot `SceneTree`, `Crypto`, hashing, JSON, and a lightweight fake clock.

#### `godot-editor-mcp/plugin/tests/phase8_service_boundary_test.gd`

**Responsibility:** Verify that edited-scene read handlers and UndoRedo-backed mutation handlers have exclusive ownership, retain valid direct callables, and keep traversal/snapshot/cursor logic out of the mutation service and undo-action creation out of the inspector.

**Internal source dependencies:** `edited_scene_inspector.gd` and `scene_commands.gd`, including their focused transitive infrastructure dependencies.

**Module dependencies:** M13 testing M7/M8/M10. Runtime dependencies: Godot `SceneTree`, callable introspection, and bounded source-file reads for the structural regression guard.

#### `godot-editor-mcp/plugin/tests/phase9_runtime_inspection_test.gd`

**Responsibility:** Verify that the runtime probe contains no network, expression, or supplied-script execution path; exercise bounded runtime metadata, grouping, visibility, pagination markers, selected live properties, hashed object identities, and stale identity/snapshot rejection against an in-process running-scene fixture.

**Internal source dependencies:** `runtime_probe.gd` and its `error_envelope.gd`, `command_limits.gd`, `project_identity.gd`, and `property_value_codec.gd` dependencies.

**Module dependencies:** M13 testing M8/M12. Runtime dependencies: Godot `SceneTree`, `Node2D`, `Sprite2D`, property metadata, groups, hashing, and bounded source-file reads.

#### `godot-editor-mcp/plugin/tests/phase10_gameplay_validation_test.gd`

**Responsibility:** Exercise runtime node/count/built-in-property conditions, run staleness, Input Map press and automatic/shutdown release, injected-input markers, fixed capture staging or explicit headless-renderer rejection, and source guards against expression/method execution.

**Internal source dependencies:** `runtime_probe.gd`, `runtime_gameplay_commands.gd`, and `error_envelope.gd`.

**Module dependencies:** M13 testing M8/M12. Runtime dependencies: Godot `SceneTree`, `Node2D`, Input/InputMap, viewport/display APIs, and bounded project-internal files.

#### `godot-editor-mcp/plugin/tests/phase11_scene_transaction_test.gd`

**Responsibility:** Verify tagged node/resource/NodePath values, class compatibility, common 2D/3D math forms, packed arrays, enums/flags, non-finite, string, and packed-byte bounds, advertised forms and limits, the complete structural-operation vocabulary, one-version commit, full-batch undo/redo, and immediate postcondition rollback through deterministic injected editor/UndoRedo collaborators.

**Internal source dependencies:** `property_value_codec.gd`, `command_limits.gd`, and `scene_transaction.gd` with their error and path/value infrastructure.

**Module dependencies:** M13 testing M8/M10. Runtime dependencies: Godot `SceneTree`, nodes, PackedScene snapshotting, Variant/math/packed-array APIs, source-file reads, and lightweight fake editor/UndoRedo/project-path collaborators.

#### `godot-editor-mcp/plugin/tests/phase12_project_workflow_test.gd`

**Responsibility:** Verify focused handler ownership, bounded autoload listing, add/remove diffs, expected-value rejection, invalid-path and protected-probe failures, save-failure rollback, project-reload requirements, and compact enabled plugin metadata through injected editor/save collaborators.

**Internal source dependencies:** `project_workflow_commands.gd`, `project_path_guard.gd`, `error_envelope.gd`, and the Node-based `tests/fixtures/phase12_autoload.gd` validation fixture.

**Module dependencies:** M13 testing M7/M8/M11. Runtime dependencies: Godot `SceneTree`, `ProjectSettings`, autoload instantiation, editor-plugin configuration, and callable injection.

#### `godot-editor-mcp/plugin/tests/fixtures/phase12_autoload.gd`

**Responsibility:** Provide the minimal valid Node-inheriting script fixture used to prove autoload resource validation without project-specific behavior.

**Internal source dependencies:** None.

**Module dependencies:** M13 fixture for the Phase 12 M11 test. Runtime dependency: Godot `Node`.

## Dependency direction summary

The intended production dependency direction is:

```text
Python entry/composition (M1)
  -> MCP presentation (M2)
  -> tool policy/dispatch (M3)
       -> local services (M4)
       -> bridge/error/discovery (M5)
       -> waiting (M6) -> M5

localhost JSON contract

Godot plugin lifecycle/transport/router (M7)
  -> shared infrastructure (M8)
  -> state/diagnostics (M9) -> M8
  -> asset/scope-aware inspection/scene transaction services (M10) -> M8, M9, M12
  -> settings/input/project workflows/reload (M11) -> M8, M9
  -> runtime debugger gateway/probe (M12) -> M8, M9
```

There are no production import cycles inside the Python package. The only reverse Python edge is a deliberate lazy compatibility import from `server.py` back to `cli.py`, which avoids an import-time cycle. On the Godot side, composition is centralized in `godot_mcp.gd`; apparent two-way relationships with the state monitor are runtime injection/callback relationships rather than script preload cycles.
