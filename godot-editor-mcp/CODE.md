# Godot Editor MCP code structure, complexity, and coupling

## Scope and method

This document covers all hand-written Python and GDScript source under `godot-editor-mcp/`, including production code and tests. It excludes generated Godot `.uid` files, the CSV fixture, package and plugin metadata, project configuration, and prose documentation because they are not source code. Configuration files are still mentioned where code depends on their values or paths.

Dependencies below include:

- **direct dependencies**: Python imports, GDScript `preload`/`extends`, and explicitly injected collaborators;
- **runtime dependencies**: Python standard-library facilities or Godot editor APIs used by a file;
- **contract dependencies**: cross-process names and data shapes that must remain synchronized even though the files cannot import one another.

The implementation contains **30 production source files** (14 Python and 16 GDScript), totaling about **4,741 lines**, and **12 test source files** totaling about **1,441 lines**. These line counts include comments and blank lines and are only structural indicators, not cyclomatic-complexity measurements.

## Implemented modules

The word *module* in this section means a cohesive architectural subsystem. The following list covers every production source file; the later file inventory maps each file back to one or more of these modules.

### M1. Python process entry and composition

**Responsibility:** Start the MCP process, parse options, construct the bridge, local asset guard, optional editor launcher, MCP server, and stdio transport. It also preserves legacy entry points.

**Files:** `godot-editor-mcp/server.py`, `godot_editor_mcp/__init__.py`, `godot_editor_mcp/__main__.py`, `godot_editor_mcp/cli.py`.

**Dependencies on other modules:** M2 for transport and request handling; M3 for mode and collaborator interfaces; M4 for local asset and editor-process services; M5 for the editor bridge; indirectly M6 because M2/M3 create operation waits.

### M2. MCP protocol presentation and stdio transport

**Responsibility:** Implement newline-delimited JSON-RPC over stdin/stdout, MCP initialization, protocol negotiation, `tools/list`, `tools/call`, tool-result encoding, error presentation, and shutdown cancellation.

**Files:** `godot_editor_mcp/stdio.py`, `godot_editor_mcp/server.py`.

**Dependencies on other modules:** M3 for schemas, modes, and execution; M5 for typed domain errors; M1 only through lazy compatibility wrappers in `server.py`.

### M3. Tool API policy and dispatch

**Responsibility:** Define one typed specification per model-facing tool and derive schemas, stable mode subsets, bridge routes, project-path and wait policy, and expected live bridge contracts; dispatch those specifications through focused bridge or local handlers; enrich capabilities and coordinate optional waits.

**Files:** `godot_editor_mcp/tool_catalog.py`, `godot_editor_mcp/tool_dispatch.py`.

**Dependencies on other modules:** M4 for asset and launcher collaborators; M5 for bridge calls and bridge errors; M6 for asynchronous completion; M1 for the package version. Contract-coupled to M7, M9, M10, and M11 through command names, argument shapes, result shapes, mode capabilities, and version reporting.

### M4. Python local project and process services

**Responsibility:** Confine filesystem access to the Godot project/import root, validate paths and extensions, atomically copy bounded staged assets, create project folders, and optionally launch the configured Godot editor as a detached process.

**Files:** `godot_editor_mcp/assets.py`, `godot_editor_mcp/launcher.py`.

**Dependencies on other modules:** M5 because the launcher probes the bridge before starting another editor. M3 consumes both services through protocols. Asset import is contract-coupled to M10 because the dispatcher asks the plugin to scan the copied file.

### M5. Python bridge, discovery, and shared error model

**Responsibility:** Discover the live project-specific plugin port, authenticate with the project token, exchange bounded newline-delimited JSON on localhost, decode structured bridge failures into typed errors, and expose bounded public error details.

**Files:** `godot_editor_mcp/bridge.py`, `godot_editor_mcp/discovery.py`, `godot_editor_mcp/errors.py`.

**Dependencies on other modules:** No higher-level Python module. Contract-coupled to M7 for transport/authentication, M8 for error envelopes and limits, M11 for reload reconnect validation, and the Godot side of discovery in M7.

### M6. Python asynchronous-operation waiting

**Responsibility:** Poll editor state for scene, import, run, stop, and project-reload completion; enforce timeouts and cancellation; require a brief diagnostic quiet period; validate run, operation, project, and bridge identities across reconnects.

**Files:** `godot_editor_mcp/waiting.py`.

**Dependencies on other modules:** M5 for bridge access and typed errors. Contract-coupled to M9 for the `state` representation and operation/import/run fields, to M10 for asset status, and to M11 for `reload_status` and persisted reload identities.

### M7. Godot plugin lifecycle, network bridge, discovery, and routing

**Responsibility:** Compose all editor-side services, register their direct callable maps with atomic duplicate-ownership checks, manage plugin startup/shutdown and polling, create/load the auth token, listen only on localhost, validate bounded requests, dispatch command names, publish a project-specific heartbeat, and report capabilities.

**Files:** `plugin/addons/godot_mcp/godot_mcp.gd`, `bridge_server.gd`, `command_router.gd`, `discovery_record.gd`.

**Dependencies on other modules:** M8 for envelopes, limits, and operation/event primitives; M9 for state and diagnostics; M10 for asset and scene commands; M11 for project settings, input map, and reload commands. Contract-coupled to M3 and M5 through commands, transport, authentication, discovery records, limits, versions, and response envelopes.

### M8. Godot shared command infrastructure and bounded records

**Responsibility:** Centralize command validation/conversion helpers, success/failure envelopes and error codes, resource bounds, operation IDs, and bounded editor event history.

**Files:** `command_base.gd`, `command_limits.gd`, `error_envelope.gd`, `operation_registry.gd`, `event_store.gd`.

**Dependencies on other modules:** The base command class accepts M9's state monitor and this module's operation registry as injected, structurally typed collaborators. Otherwise this is the low-level Godot module on which M7, M9, M10, and M11 depend. It is contract-coupled to M3/M5/M6 through limits, error codes, operation IDs, and response shapes.

### M9. Godot editor state and diagnostics

**Responsibility:** Observe scenes, undo history, play state, filesystem scans/imports, project-file changes, operations, and diagnostics; expose concise editor state; control save/run/stop; collect bounded parser/editor/runtime diagnostics.

**Files:** `editor_state_monitor.gd`, `diagnostic_store.gd`.

**Dependencies on other modules:** M8 for envelopes, event IDs, operation IDs, and bounded error semantics. Its instances are composed and polled by M7 and called by M10/M11 through injected references. Contract-coupled to M6 because Python waits parse its state fields.

### M10. Godot asset and scene commands

**Responsibility:** List and inspect assets, request scans, create resources and scenes, open scenes, inspect/edit scene trees, instantiate scenes, set properties through undo history, and select nodes.

**Files:** `asset_commands.gd`, `scene_commands.gd`.

**Dependencies on other modules:** M8 for the command base, validation/conversion helpers, limits, error envelopes, and operation IDs; M9 for import tracking. M7 registers these services. Contract-coupled to M3/M4/M6 through command and result shapes and the copy-then-scan workflow.

### M11. Godot project settings, input map, and reload commands

**Responsibility:** Read and atomically patch bounded project settings, normalize and patch Input Map events, track reload requirements, safely restart the editor, persist/recover a reload operation across restart, and report reload completion.

**Files:** `project_settings_commands.gd`, `input_map_commands.gd`, `reload_commands.gd`.

**Dependencies on other modules:** M8 for base validation, limits, envelopes, and operation IDs; M9 for marking known project-settings writes. M7 registers and polls the services. Contract-coupled to M3 and M6 through tool/command arguments and reload identity/status fields.

### M12. Automated verification

**Responsibility:** Verify Python units and MCP integration plus focused headless-Godot behavior, including registry and release contracts, live capabilities, duplicate-safe routing, bridge security/bounds, discovery, filesystem confinement, modes and routing, waiting, launcher portability, diagnostics, and reload persistence/reconnect.

**Files:** all files under `godot-editor-mcp/tests/` ending in `.py` and `godot-editor-mcp/plugin/tests/` ending in `.gd`.

**Dependencies on other modules:** Directly tests M2-M6 and selected pieces of M8-M11. The reload integration test additionally launches Godot with the plugin from M7 and therefore covers the Python/GDScript boundary.

## Complexity and coupling assessment

### Main complexity concentrations

| Area | Indicator | Assessment |
| --- | ---: | --- |
| `tool_catalog.py` | 499 lines | Large but mostly declarative tool specifications and JSON Schemas. It now centralizes routing, modes, paths, waits, limits, and live-contract expectations; control-flow complexity remains low while cross-language policy is intentionally concentrated. |
| `editor_state_monitor.gd` | 361 lines | Highest state-machine complexity: it correlates editor polling, signals, imports, runs, scene saves, project hashing, operations, events, and diagnostics. Five injected/editor collaborators make it a major integration hub. |
| `reload_commands.gd` | 324 lines | Stateful restart/recovery protocol with persisted records, time validation, dirty-scene safeguards, deferred cleanup, and identity checks. High temporal complexity despite only two public commands. |
| `waiting.py` | 302 lines | Multiple polling state machines with timeouts, cancellation, diagnostic settling, reconnect tolerance, and stale-identity checks. Strongly coupled to the exact editor-state contract. |
| `project_settings_commands.gd` | 298 lines | Recursive type conversion plus validation, compare-and-swap behavior, transactional rollback, and reload classification. High branching and data-shape complexity. |
| `asset_commands.gd` | 250 lines | Combines filesystem traversal, Godot resource introspection, creation, scanning, and operation tracking. Bounds reduce operational risk but add validation branches. |
| `godot_mcp.gd` | 240 lines, 14 direct source preloads | Intentional composition root and the highest direct fan-out. Changes here affect service construction, duplicate-safe command registration, capabilities, versioning, and lifecycle. |
| `errors.py` / `diagnostic_store.gd` | 226 / 224 lines | Broad shared contracts. They are not algorithmically dominant, but many modules rely on their stable bounded shapes and identifiers. |
| `tool_dispatch.py` | 206 lines, 6 direct internal imports | Main Python orchestration hub. It resolves registry policy into focused local or bridge execution, preflight validation, waits, capability enrichment, and error normalization. |
| `input_map_commands.gd` | 213 lines | Detailed event normalization and transactional mutation across four input-event families. |

### Coupling hotspots and change risks

1. **The bridge protocol is intentionally duplicated across languages and guarded by tests.** `tool_catalog.py` owns Python policy, while `godot_mcp.gd`, `command_router.gd`, and command-service handler maps own editor registration. Live capability and invariant tests fail on command, protocol, limit, error-code, wait-field, or version drift.
2. **Errors are a mirrored contract.** `errors.py` and `error_envelope.gd` duplicate stable error-code strings and bounded detail behavior. Unknown plugin codes degrade to generic `BridgeError`, but omitted or mismatched known codes weaken typed handling.
3. **Versions and capabilities remain duplicated but are release-checked.** `godot_editor_mcp/__init__.py`, `godot_mcp.gd`, package/plugin metadata, capability responses, and reload records all carry version information. The current source versions are `0.8.0`, and automated checks compare the release records and live plugin response.
4. **Limits are mirrored with explicit regression checks.** Python byte limits and schemas must match `command_limits.gd` and live capabilities. Changing only one side fails the contract suite before it can cause rejected requests, oversized responses, or misleading metadata.
5. **Discovery and reload records are cross-language persistent schemas.** `discovery.py` must match `discovery_record.gd`. `waiting.py` must match `reload_commands.gd`. Project-path normalization, hashes, timestamps, record versions, operation IDs, and bridge versions are all compatibility-sensitive.
6. **`editor_state_monitor.gd` is a shared mutable-state hub.** Asset scans, scene open/run/stop, project settings, diagnostics, operations, and Python waiters all converge on it. This cohesion is purposeful, but regressions can affect apparently unrelated tools.
7. **`command_base.gd` creates inheritance coupling.** Four service scripts inherit its path validation, value encoding/conversion, operation acceptance, and response helpers. A base-class behavior change has broad reach.
8. **Dependency injection contains Python test coupling.** `MCPServer`, `ToolDispatcher`, `OperationWaiter`, and `EditorLauncher` accept small structural interfaces, so tests use fakes without extra dependencies. This lowers concrete coupling even though orchestration fan-out remains high.
9. **No third-party runtime dependency is present in the source.** Python uses only the standard library and GDScript uses Godot APIs. This keeps deployment coupling low and supports offline operation.

### Overall assessment

The codebase is moderately sized and intentionally split by boundary: MCP presentation, Python orchestration, local filesystem/process operations, localhost transport, and editor-side command services. Most files are cohesive. The primary maintenance risk is not raw file size but **cross-language contract coupling** and **temporal state** around imports, play/stop, diagnostics, discovery, and reload. Tests cover the major seams, including one subprocess integration path for reload, which is the right place to contain that risk.

## Source file inventory and dependencies

### Python production files

#### `godot-editor-mcp/server.py`

**Responsibility:** Legacy executable shim that invokes the packaged CLI.

**Internal source dependencies:** `godot_editor_mcp/cli.py`.

**Module dependencies:** M1. No direct standard-library dependency.

#### `godot-editor-mcp/godot_editor_mcp/__init__.py`

**Responsibility:** Define the package identity and authoritative Python package version (`0.8.0`).

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

**Responsibility:** Read one JSON-RPC object per input line, write protocol-safe compact JSON responses to stdout, send diagnostics only to stderr, and close the handler on shutdown.

**Internal source dependencies:** None; depends on a structural `RequestHandler` supplied by `server.py`.

**Module dependencies:** M2. Runtime dependencies: Python `json`, `sys`, and typing/text-stream protocols.

#### `godot-editor-mcp/godot_editor_mcp/server.py`

**Responsibility:** Negotiate MCP versions, validate JSON-RPC requests, expose mode-filtered tools, invoke the dispatcher, encode normal/tool errors, and cancel waits when closed; retain older public imports and entry points.

**Internal source dependencies:** `__init__.py`, `errors.py`, `stdio.py`, `tool_catalog.py`, `tool_dispatch.py`; lazy compatibility imports from `cli.py`.

**Module dependencies:** M2 primarily; M1, M3, and M5. Runtime dependency: Python typing.

#### `godot-editor-mcp/godot_editor_mcp/tool_catalog.py`

**Responsibility:** Hold one typed tool specification for each MCP tool and derive public schemas, strict nested modes, stable orders, bridge routes, path/wait policies, and expected plugin commands, protocol version, limits, and error codes.

**Internal source dependencies:** None.

**Module dependencies:** M3. Runtime dependencies: Python `dataclasses` and typing. Contract dependencies: `tool_dispatch.py` and all registered editor-side commands in M7/M9-M11.

#### `godot-editor-mcp/godot_editor_mcp/tool_dispatch.py`

**Responsibility:** Route MCP tools to local asset/launcher services or short bridge commands; perform project-path preflight checks; strip Python-only wait fields; coordinate operation waits; enrich capability results.

**Internal source dependencies:** `__init__.py`, `assets.py`, `bridge.py`, `launcher.py`, `tool_catalog.py`, `waiting.py`.

**Module dependencies:** M3 directly; M1, M4-M6. Runtime dependency: Python typing protocols. Contract dependencies: command registrations in `godot_mcp.gd`/`command_router.gd` and response fields from M9-M11.

#### `godot-editor-mcp/godot_editor_mcp/assets.py`

**Responsibility:** Validate project/import roots, reject traversal/symlink/protected destinations, validate asset paths, create confined folders, and copy one bounded staged asset atomically without overwrite.

**Internal source dependencies:** None.

**Module dependencies:** M4. Runtime dependencies: Python `os`, `tempfile`, `pathlib`, and typing. Workflow dependency: `tool_dispatch.py` requests a Godot scan after successful writes.

#### `godot-editor-mcp/godot_editor_mcp/launcher.py`

**Responsibility:** Probe for an already-running editor, validate `GODOT_EXECUTABLE`, and start the configured project in a detached POSIX session or Windows process group.

**Internal source dependencies:** `bridge.py` (`GodotBridge`, `BridgeError`).

**Module dependencies:** M4 and M5. Runtime dependencies: Python `os`, `pathlib`, `subprocess`, and typing.

#### `godot-editor-mcp/godot_editor_mcp/bridge.py`

**Responsibility:** Validate project/localhost configuration, read the auth token, discover the port, send bounded command requests over TCP, read bounded responses, and decode bridge failures.

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

**Responsibility:** Implement cancellable bounded polling for open scene, asset import, run startup, stop, and reload/reconnect completion, including diagnostic settling and stale identity detection.

**Internal source dependencies:** `errors.py`; accepts a bridge-compatible collaborator structurally equivalent to `bridge.py`.

**Module dependencies:** M6 and M5. Runtime dependencies: Python `time`, `threading.Event`, and typing. Contract dependencies: `editor_state_monitor.gd`, `operation_registry.gd`, `asset_commands.gd`, and `reload_commands.gd`.

### Godot plugin production files

#### `godot-editor-mcp/plugin/addons/godot_mcp/godot_mcp.gd`

**Responsibility:** Main `EditorPlugin` composition root: create shared services, register every command, manage token/port/listener/discovery lifecycle, poll asynchronous services, publish capabilities, and synchronize scene-save notifications.

**Internal source dependencies:** `asset_commands.gd`, `bridge_server.gd`, `command_router.gd`, `discovery_record.gd`, `diagnostic_store.gd`, `editor_state_monitor.gd`, `error_envelope.gd`, `event_store.gd`, `input_map_commands.gd`, `command_limits.gd`, `operation_registry.gd`, `project_settings_commands.gd`, `reload_commands.gd`, `scene_commands.gd`.

**Module dependencies:** M7 directly and M8-M11 through composition. Runtime dependencies: Godot `EditorPlugin`, `EditorInterface`, `EditorUndoRedoManager`, `ProjectSettings`, `OS`, `Crypto`, and file APIs. Contract dependencies: Python M3/M5 and plugin metadata/version.

#### `godot-editor-mcp/plugin/addons/godot_mcp/bridge_server.gd`

**Responsibility:** Maintain the localhost TCP listener and clients, buffer one bounded newline-delimited request, authenticate the token in constant-time style, invoke the injected router, bound responses, and disconnect.

**Internal source dependencies:** `error_envelope.gd`, `command_limits.gd`; injected dispatch callable from `command_router.gd`.

**Module dependencies:** M7 and M8. Runtime dependencies: Godot `TCPServer`, `StreamPeerTCP`, `PackedByteArray`, and `JSON`. Contract dependency: Python `bridge.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/command_router.gd`

**Responsibility:** Atomically register direct callables with explicit owners, reject duplicate command ownership, report sorted commands, and dispatch a command or return a structured unknown-command failure.

**Internal source dependencies:** `error_envelope.gd`; receives all service instances from `godot_mcp.gd`.

**Module dependencies:** M7 and M8, with injected dependencies on M9-M11.

#### `godot-editor-mcp/plugin/addons/godot_mcp/discovery_record.gd`

**Responsibility:** Hash the normalized project path, atomically publish a periodic bridge heartbeat record, and remove only the current process's record on shutdown.

**Internal source dependencies:** None.

**Module dependencies:** M7. Runtime dependencies: Godot `ProjectSettings`, `OS`, `Time`, `HashingContext`, `FileAccess`, `DirAccess`, and `JSON`. Contract dependency: Python `discovery.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/command_base.gd`

**Responsibility:** Base class for command services, providing editor/undo references, confined node/resource path checks, JSON-to-Godot conversion and bounded encoding, input-event normalization, operation acceptance, and response helpers.

**Internal source dependencies:** `error_envelope.gd`; injected `operation_registry.gd` and optional `editor_state_monitor.gd` collaborators. Extended by `asset_commands.gd`, `scene_commands.gd`, `project_settings_commands.gd`, and `input_map_commands.gd`.

**Module dependencies:** M8, with optional M9 coupling. Runtime dependencies: broad Godot object, scene, filesystem, variant, resource, input-event, editor, and undo APIs.

#### `godot-editor-mcp/plugin/addons/godot_mcp/command_limits.gd`

**Responsibility:** Centralize editor-side byte, traversal, result, settings, input, and diagnostic bounds.

**Internal source dependencies:** None.

**Module dependencies:** M8. Contract dependencies: Python `bridge.py`, `tool_catalog.py`, and capability reporting in `godot_mcp.gd`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/error_envelope.gd`

**Responsibility:** Define stable editor-side error codes and bounded success/failure envelopes, classify legacy message failures, and extract public messages.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependency: Godot Variant/container/string APIs. Contract dependency: Python `errors.py` and `bridge.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/operation_registry.gd`

**Responsibility:** Allocate process-scoped operation IDs, retain a bounded registry, mark operations complete by ID or kind, recover completed operations, and expose concise active/recent views.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependency: Godot `OS` and container APIs. Consumed by M9-M11 and `command_base.gd`; contract-coupled to Python `waiting.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/event_store.gd`

**Responsibility:** Allocate monotonic editor event IDs and retain a bounded timestamped event history.

**Internal source dependencies:** None.

**Module dependencies:** M8. Runtime dependency: Godot `Time` and containers. Consumed by `editor_state_monitor.gd`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/editor_state_monitor.gd`

**Responsibility:** Observe and summarize editor state; correlate scene, run, filesystem, import, operation, event, project-file, and diagnostic state; implement save/run/stop commands.

**Internal source dependencies:** `error_envelope.gd`; injected `event_store.gd`, `operation_registry.gd`, and `diagnostic_store.gd` collaborators.

**Module dependencies:** M9 and M8. Runtime dependencies: Godot editor, undo history, resource filesystem/signals, resource loading, project settings, hashing, file, time, and play-control APIs. Contract dependency: Python `waiting.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/diagnostic_store.gd`

**Responsibility:** Act as a thread-safe Godot logger, sanitize and categorize bounded parser/editor/runtime records, associate runtime records with run IDs, support cursor/filter reads, and supply counts/import errors.

**Internal source dependencies:** `error_envelope.gd`.

**Module dependencies:** M9 and M8. Runtime dependencies: Godot `Logger`, `Mutex`, `ScriptBacktrace`, `ProjectSettings`, `Time`, and error types. Consumed by `godot_mcp.gd` and `editor_state_monitor.gd`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/asset_commands.gd`

**Responsibility:** List bounded assets, inspect one asset and dependencies, request scans, create whitelisted resources/scenes, and open packed scenes.

**Internal source dependencies:** extends `command_base.gd`; preloads `command_limits.gd`; uses injected `operation_registry.gd` and `editor_state_monitor.gd` through the base class.

**Module dependencies:** M10, M8, and M9. Runtime dependencies: Godot editor resource filesystem, `DirAccess`, `FileAccess`, `ResourceLoader`, `ResourceSaver`, `ClassDB`, `PackedScene`, and resource APIs. Contract dependencies: Python `tool_dispatch.py`, `assets.py`, and `waiting.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/scene_commands.gd`

**Responsibility:** Return bounded scene trees/properties; add built-in nodes; instantiate packed scenes; set editable properties through undo history; and select nodes.

**Internal source dependencies:** extends `command_base.gd`; preloads `command_limits.gd`.

**Module dependencies:** M10 and M8. Runtime dependencies: Godot scene tree, `ClassDB`, `ResourceLoader`, `PackedScene`, selection, property metadata, and editor undo APIs. Contract dependency: Python `tool_catalog.py`/`tool_dispatch.py`.

#### `godot-editor-mcp/plugin/addons/godot_mcp/project_settings_commands.gd`

**Responsibility:** Read individual/prefixed settings, block secret/internal/input keys, convert bounded JSON-safe values, apply compare-and-swap patches transactionally, roll back failed saves, and classify reload requirements.

**Internal source dependencies:** extends `command_base.gd`; preloads `command_limits.gd`; calls injected `editor_state_monitor.gd` after known writes.

**Module dependencies:** M11, M8, and M9. Runtime dependencies: Godot `ProjectSettings`, Variant/type APIs, vectors/colors, arrays, dictionaries, and input-event encoding. Contract dependency: Python tool schemas/dispatch in M3.

#### `godot-editor-mcp/plugin/addons/godot_mcp/input_map_commands.gd`

**Responsibility:** Validate and normalize key/mouse/joypad events, add/remove bindings without duplicates, change deadzones, update `InputMap`, save transactionally, and roll back failures.

**Internal source dependencies:** extends `command_base.gd`; preloads `command_limits.gd`; calls injected `editor_state_monitor.gd` after saved writes.

**Module dependencies:** M11, M8, and M9. Runtime dependencies: Godot `ProjectSettings`, `InputMap`, `InputEventKey`, `InputEventMouseButton`, `InputEventJoypadButton`, `InputEventJoypadMotion`, and OS key-code APIs. Contract dependency: Python tool schema/dispatch in M3.

#### `godot-editor-mcp/plugin/addons/godot_mcp/reload_commands.gd`

**Responsibility:** Enforce run/dirty-scene safeguards, persist a bounded pending reload record atomically, defer editor restart until after response delivery, recover/validate completion after restart, report status, and defer cleanup until completion is returned.

**Internal source dependencies:** `error_envelope.gd`; injected `operation_registry.gd`.

**Module dependencies:** M11 and M8; polled by M7. Runtime dependencies: Godot editor restart/save APIs, `ProjectSettings`, `OS`, `Time`, `HashingContext`, `FileAccess`, `DirAccess`, and `JSON`. Contract dependency: Python `waiting.py`.

### Python test files

#### `godot-editor-mcp/tests/test_assets.py`

**Responsibility:** Verify allowed imports, disabled import roots, size bounds, no-overwrite behavior, confined folder creation, traversal/symlink/protected-path rejection, and scene path validation.

**Internal source dependencies:** `godot_editor_mcp/assets.py`.

**Module dependencies:** M12 testing M4. Runtime dependencies: Python `os`, `tempfile`, `unittest`, and `pathlib`.

#### `godot-editor-mcp/tests/test_bridge.py`

**Responsibility:** Verify authenticated request encoding, live discovery port selection, safe legacy/structured error decoding, typed not-found errors, and project validation with fake sockets.

**Internal source dependencies:** `bridge.py`, `discovery.py`, `errors.py`.

**Module dependencies:** M12 testing M5. Runtime dependencies: Python `json`, `tempfile`, `time`, `unittest`, `pathlib`, and mocking.

#### `godot-editor-mcp/tests/test_contracts.py`

**Responsibility:** Verify unique registry names, stable public/catalog ordering, nested modes, complete local and bridge routes, schema-aligned path/wait policy and limits, mirrored editor error codes, live capability comparison behavior, and dependency-free release version consistency.

**Internal source dependencies:** `__init__.py`, `bridge.py`, `errors.py`, `tool_catalog.py`; reads package/plugin/runtime metadata plus `HISTORY.md` and `ROADMAP.md` as release records.

**Module dependencies:** M12 testing M1, M3, M5, and cross-language contracts with M7/M8. Runtime dependencies: Python `re`, `unittest`, and `pathlib`.

#### `godot-editor-mcp/tests/test_discovery.py`

**Responsibility:** Verify live, stale, malformed, and cross-project discovery-record handling.

**Internal source dependencies:** `discovery.py`, `errors.py`.

**Module dependencies:** M12 testing M5. Runtime dependencies: Python `json`, `tempfile`, `time`, `unittest`, and `pathlib`.

#### `godot-editor-mcp/tests/test_launcher.py`

**Responsibility:** Verify missing/relative executable failures, POSIX and Windows detach flags, safe process-start errors, and avoidance of duplicate editor starts.

**Internal source dependencies:** `bridge.py`, `launcher.py`.

**Module dependencies:** M12 testing M4/M5. Runtime dependencies: Python `tempfile`, `unittest`, `pathlib`, and mocking.

#### `godot-editor-mcp/tests/test_reload_integration.py`

**Responsibility:** Optional subprocess integration test that copies the plugin into a temporary project, launches Godot headlessly, verifies the live command/protocol/version/limit/error-code capability contract, exercises reload safeguards, persists/reconnects an authenticated operation, and checks typed busy/save failures.

**Internal source dependencies:** `bridge.py`, `discovery.py`, `errors.py`, `tool_dispatch.py`; runtime dependency on the complete `plugin/addons/godot_mcp/` source and `plugin/project.godot` fixture.

**Module dependencies:** M12 testing M3, M5-M8, M9, and M11 across the process boundary. Runtime dependencies: Python `os`, `shutil`, `signal`, `socket`, `subprocess`, `sys`, `tempfile`, `time`, `unittest`, and `pathlib`, plus an installed Godot executable.

#### `godot-editor-mcp/tests/test_server.py`

**Responsibility:** Verify MCP initialization/tool listing, nested mode subsets, forbidden-tool rejection, command mapping, diagnostics availability, local-only wait fields, reload waits, import/scan flow, path validation, settings routing, capabilities, launcher exposure, error encoding, and notification behavior using fakes.

**Internal source dependencies:** `bridge.py`, `errors.py`, `server.py` (which transitively exercises `tool_catalog.py`, `tool_dispatch.py`, `waiting.py`, and `stdio.py`).

**Module dependencies:** M12 testing M2-M6. Runtime dependencies: Python `json` and `unittest`.

#### `godot-editor-mcp/tests/test_stdio.py`

**Responsibility:** Verify end-to-end newline-delimited initialize/list/call output and ensure parse/internal diagnostics do not corrupt stdout.

**Internal source dependencies:** `server.py`, `stdio.py`.

**Module dependencies:** M12 testing M2 and indirectly M3. Runtime dependencies: Python `io.StringIO`, `json`, and `unittest`.

#### `godot-editor-mcp/tests/test_waiting.py`

**Responsibility:** Verify wait option validation, argument stripping, scene/import/run/stop completion, startup failure observation, timeout/cancellation, and reload reconnect/stale project/version/operation handling with deterministic fake clocks and bridges.

**Internal source dependencies:** `errors.py`, `waiting.py`.

**Module dependencies:** M12 testing M5/M6. Runtime dependency: Python `unittest`.

### GDScript test files

#### `godot-editor-mcp/plugin/tests/phase2_diagnostics_test.gd`

**Responsibility:** Headless Godot smoke test for diagnostic filtering, cursor behavior, bounded records, and stale cursor errors.

**Internal source dependencies:** `diagnostic_store.gd` (which transitively preloads `error_envelope.gd`).

**Module dependencies:** M12 testing M8/M9. Runtime dependencies: Godot `SceneTree` and logging APIs.

#### `godot-editor-mcp/plugin/tests/phase3_reload_record_test.gd`

**Responsibility:** Headless Godot unit test for pending reload-record validation, including malformed, stale, project-mismatch, and bridge-version-mismatch cases.

**Internal source dependencies:** `error_envelope.gd`, `reload_commands.gd`.

**Module dependencies:** M12 testing M8/M11. Runtime dependency: Godot `SceneTree`, time, and hashing/project APIs used by the tested script.

#### `godot-editor-mcp/plugin/tests/phase4_command_router_test.gd`

**Responsibility:** Headless Godot unit test for direct callable dispatch, stable command reporting, atomic registration, duplicate ownership rejection, and structured unknown-command failures.

**Internal source dependencies:** `command_router.gd` (which preloads `error_envelope.gd`).

**Module dependencies:** M12 testing M7/M8. Runtime dependency: Godot `SceneTree` and callable/container APIs.

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
  -> asset/scene commands (M10) -> M8, M9
  -> settings/input/reload (M11) -> M8, M9
```

There are no production import cycles inside the Python package. The only reverse Python edge is a deliberate lazy compatibility import from `server.py` back to `cli.py`, which avoids an import-time cycle. On the Godot side, composition is centralized in `godot_mcp.gd`; apparent two-way relationships with the state monitor are runtime injection/callback relationships rather than script preload cycles.
