# Repository Guidelines

## Purpose and Operating Environment

This repository contains lightweight tools for local LLM workflows, primarily MCP servers for LM Studio. Development and normal operation must work without ChatGPT or internet access. The supported desktop platforms are macOS, Linux, and Windows. The primary development target is a 16 GB MacBook Pro running small local models, so minimize memory use, startup time, dependencies, and context consumption.

## Core Design Constraints

- Prefer offline-capable, standard-library implementations.
- Do not require cloud services, telemetry, accounts, or runtime downloads.
- Keep processes small enough to run alongside LM Studio.
- Avoid background services when a short-lived or stdio process is sufficient.
- Keep runtime behavior and documented setup portable across macOS, Linux, and Windows. Isolate unavoidable platform-specific process behavior and test each branch without adding runtime dependencies.
- Pin unavoidable dependencies and document how to prepare them before going offline.

## Refactoring and Architecture

Refactoring and major architectural changes are always valid options when they would materially improve project structure, maintainability, clarity, performance, security, or testability. Do not assume that work must be limited to small incremental changes when a broader redesign is the better long-term solution.

When analysis or implementation reveals modules that would benefit from optimization or refactoring, always propose a concrete recommendation. Identify the affected modules, expected maintainability or structural benefit, scope, risks, and relevant tradeoffs. A recommendation does not by itself authorize implementation outside the user's requested scope; present it for consideration when the broader change has not already been requested.

Prefer low coupling between modules and isolate the context needed for each change. Keep responsibilities and interfaces narrow, avoid introducing dependencies between unrelated modules, and preserve boundaries that allow an agent or maintainer to understand and modify one feature without loading the entire codebase.

For every feature request, read the target application's `CODE.md` before inspecting or changing source code. For work spanning applications, read each affected application's `CODE.md`. Use the module and file dependency maps to identify the smallest relevant working set, then work only with files and modules related to the feature. Related files include direct and transitive dependencies that the feature affects, corresponding tests, and any documentation, configuration, examples, metadata, history, or roadmap records that repository rules require updating. Expand beyond that working set only when source evidence reveals an undocumented dependency; when this happens, update the affected application's `CODE.md` as part of the change.

## MCP Design for Small Models

Tool definitions and results consume the model's context. Small models also have weaker tool-selection and argument accuracy. Expose a small, focused tool set with short, distinct names, brief descriptions, simple schemas, and few arguments. Avoid redundant tools and long instructions. Return concise, predictable results with clear errors and bounded output. Prefer identifiers and paths relative to a configured root; do not expose long absolute paths to the model.

MCP stdio servers must write only protocol messages to stdout. Send diagnostics to stderr. Preserve LM Studio compatibility and test initialization, `tools/list`, and `tools/call` end to end.

## Repository Organization

Each application belongs in its own top-level folder with its source, tests, README, and configuration examples. Current applications:

- `rooted-files-mcp/`: root-confined, text-only filesystem MCP server.
- `godot-editor-mcp/`: authenticated localhost bridge and Godot 4.7 editor plugin.

Keep shared documentation at the workspace root. Introduce shared libraries only when multiple applications need the same behavior.

## Development and Testing

Document run and test commands in each application's README. Prefer built-in test frameworks and fast offline suites. Test normal behavior, invalid input, resource limits, and security boundaries. Run the complete suite after behavior changes.

Production code, test code, generated code, and release checks must not depend on the contents of documentation files. Documentation includes `README.md`, `ROADMAP.md`, `HISTORY.md`, `CODE.md`, and other prose files. Treat documentation as an output of development, not as an executable input, source of runtime truth, or test fixture. Tests must derive expected behavior and release consistency from code, package or plugin metadata, runtime-reported contracts, and behavioral results instead. This restriction does not prevent maintainers or agents from reading documentation as guidance.

Organize roadmaps as working feature increments. Every phase must deliver a complete, usable feature or cohesive feature set, including its implementation, relevant tests, and all affected documentation and examples. A phase may depend on completed earlier phases, but completing it must leave the project in a working, releasable state. Do not defer testing or documentation to a separate final phase.

Every new `ROADMAP.md`, and every roadmap revision that introduces a new feature set, must include or update a top-level phase checklist. Give each phase a checkbox, phase number, and very brief one-line description. Keep checklist status and wording synchronized with the detailed phase sections as implementation progresses or scope changes.

After implementing each roadmap phase, increase the application's version when that application uses versioning. Use a patch-version increase for bug-fix-only phases and a minor-version increase for phases that add features. Increase the major version only when the user explicitly requests it. Keep package metadata, runtime-reported versions, plugin manifests, documentation, examples, and history records consistent with the new version wherever they apply.

When every roadmap phase has been implemented and the user asks to clean up the roadmap, keep the application's `ROADMAP.md` file instead of deleting it. Replace the completed implementation plan with a concise statement that there are currently no feature requests.

After completing implementation work, update every affected document, README, configuration example, and roadmap before handing off the change.

## Godot Editor MCP Known Issues

- The plugin targets Godot 4 and is verified with Godot 4.7 stable. Re-run the headless plugin load and bridge checks when changing editor APIs or claiming compatibility with another version.
- The Python server defaults to `--mode tiny`. Use `small` for asset/import workflows and `large` when the model also controls the Godot desktop. Tool calls outside the active mode are rejected as well as omitted from `tools/list`.
- Large mode includes `start_editor`. It accepts no arguments and starts only the configured project using the absolute executable path in `GODOT_EXECUTABLE`; the plugin must already be installed and enabled. It creates a new session on POSIX and a detached process group on Windows. Tiny and small modes do not expose it.
- macOS is the currently verified development platform. Linux and Windows are supported by implementation and documentation, but native runtime validation remains pending; record results and platform-specific issues when those checks are performed.
- Deploy and update the Python package and installed Godot plugin together with exactly matching versions. Do not add backward-compatibility paths between mismatched Python and GDScript releases; reject version mismatches explicitly. The `capabilities` tool reports compatibility data, active mode, exposed tools, optional features, and effective limits.
- Version 0.11.0 adds targeted, paginated inspection. `scene_tree` defaults to root `.`, depth 3, and 50 nodes and accepts exact class filtering; `node_info` defaults to 24 properties and accepts exact property-name and Godot-category filters. `list_assets`, `scene_tree`, and `node_info` return `snapshot_id`, `truncated`, `continuation_available`, and an opaque cursor when another page exists.
- Inspection cursors are bounded server-side IDs: 48 characters, at most 128 active, and a two-minute lifetime. Repeat the same normalized query when continuing. Filesystem generation invalidates asset cursors; scene identity, UndoRedo version, or bounded structure changes invalidate edited-tree cursors; node identity or property-list changes invalidate property cursors with `stale_cursor`. Plugin restart or cursor expiry loses server-side cursor state and returns an invalid/expired cursor error.
- Version 0.13.0 adds read-only runtime inspection through `tree_scope: "runtime"`. The editor plugin manages the reserved `GodotMCPRuntimeProbe` autoload only when its path matches, refuses a conflicting autoload, and removes its own registration on shutdown. The probe uses Godot's debugger channel, remains inert without an active debugger, opens no runtime port, and exposes no runtime mutation, arbitrary method execution, or supplied-code evaluation.
- Runtime inspection initially supports one active debugger session. Its handshake binds project, run, debugger session, probe version, supported commands, effective limits, and a per-process nonce. Runtime IDs bind the run, session, and Godot object identity; runtime cursors additionally bind the runtime-tree generation. Stop, replacement run, debugger reconnect, or structural change can therefore return `stale_runtime_id` or `stale_cursor`; absent, incompatible, or multiple sessions return explicit capability errors.
- Version 0.14.0 upgrades the runtime probe protocol to version 2 and adds `capture_game_view`, `send_input`, and `wait_for_runtime_condition` in `small` and `large` modes. All three require the exact active run ID. Captures come only from the main game viewport, stage under `.godot/godot_mcp/captures/<capture-id>.png`, are bounded by source/output dimensions, pixels, PNG bytes, and time, and are independently confined, validated, returned as MCP image content, and deleted by Python. A truly headless game display returns `unsupported_capability`; abandoned captures expire after two minutes.
- Injected input accepts only existing Input Map actions and bounded strength plus duration or frame holds. The runtime schedules release, releases every injected action on probe/run shutdown, caps concurrent holds, and marks injected-action diagnostics separately from physical input. Runtime conditions are limited to play transitions, node existence, bounded node counts, and comparisons on built-in scalar Godot properties; they support no expressions, scripts, regexes, method calls, signal subscriptions, nested property traversal, or condition composition.
- Godot 4.7 headless editor mode activates scenes requested through `open_scene`, so headless mutation tests can create or open a scene through the bridge before editing it.
- Source imports are asynchronous. `import_asset` and public `scan_asset` may report `queued` or `already_running`; `asset_info` can temporarily report an empty type until Godot finishes scanning. Check `editor_state.filesystem_scanning` and its generation counter before starting another full scan.
- Imports copy one file at a time. A `.gltf` with external buffers or textures requires importing every dependency separately; prefer `.glb` for a self-contained 3D asset.
- Imports never overwrite files. Destination folders must already exist or be created with `create_folder`. Move, rename, and delete operations are intentionally not exposed.
- Only one editor bridge can listen on a port. The plugin publishes an atomic, token-free `.godot/godot_mcp_bridge.json` heartbeat keyed by a hash of the project path; the Python server discovers a live matching record unless `--port` is given explicitly. The `godot_mcp/port` project setting and explicit MCP `--port` argument must match when overriding discovery.
- Bridge failures use bounded envelopes with stable codes, messages, details, and retryability. The Python bridge still accepts legacy string failures during upgrades. Scene scan/open/run/stop requests return operation IDs, editor state reports event IDs, and `scene_control` stop requires the active run ID returned by run.
- `create_scene` and `add_node` accept built-in Node classes only, not project script classes. `create_resource` is limited to the whitelist documented in `godot-editor-mcp/README.md`.
- Version 0.15.0 adds `scene_transaction` in `small` and `large` modes. A transaction accepts at most 64 operations, creates at most 32 nodes, traverses at most 32 levels, and retains at most 256 KiB of undo data. It prevalidates against a shadow scene, supports request-local handles, optional scene and UndoRedo version preconditions, and commits as one scene-associated UndoRedo action; any failed operation or postcondition immediately rolls the action back.
- Transaction node references must use an exact scene-relative `path` or a request-local `handle`. Handles are available only after their creating, rename, or reparent operation. Structural edits reject inherited scenes, instantiated editable children, root removal or reparenting, and changes that would violate scene ownership. Python and GDScript releases remain an exact-version pair for these contracts.
- Version 0.15.0 centralizes model-facing property conversion in a bounded tagged-value codec. JSON values are limited to depth 8, 256 aggregate items, 128 keys, 2,048-character strings, and 32 KiB encoded size; packed arrays allow at most 4,096 items. Math types, packed arrays, enum/flag names, `NodePath`, node references, and resource references use explicit documented tags. Never infer node or resource references from plain strings, and continue to reject arbitrary objects or incompatible property class hints.
- Project Settings and Input Map tools are exposed in `small` and `large` modes. Generic patches reject `input/`, editor/internal, and secret-bearing keys; use `input_map_patch` for duplicate-free key, mouse, and joypad bindings. Settings changes report whether an editor refresh, project reload, or editor restart is required.
- Version 0.16.0 adds `list_autoloads`, `autoload_patch`, and read-only `list_editor_plugins` in `small` and `large` modes. Autoload and editor-plugin results are capped at 64 records, and one patch accepts at most 32 changes. Arbitrary editor-plugin enable, disable, install, and removal operations remain intentionally unavailable.
- Autoload patches prevalidate the complete add/update/remove batch and support optional expected-value comparison. Names must be valid singleton identifiers without built-in, engine-singleton, or global-class conflicts; paths must be existing `res://` Node scripts or scenes. The `GodotMCPRuntimeProbe` name and bundled path are protected. Apply changes through Godot's autoload and ProjectSettings APIs, roll back the complete batch after a save failure, and treat every effective change as requiring `reload_project`.
- A root-level `list_assets` includes editor addon files and may include Godot-generated import resources. Prefer a project asset folder and type filter to reduce small-model context use.
- `editor_state` exposes an absolute project path for issue identification. Other model-facing paths remain project-relative or use `res://`.
- A forced headless-editor shutdown can emit a dummy-renderer `Parameter "t" is null` diagnostic after resource preview work. Treat it as a headless shutdown artifact only when the plugin loaded successfully and no earlier script error is present.

## Security and Resource Limits

Treat model-generated arguments as untrusted. Validate types, lengths, paths, and operations. Deny access outside configured roots, including traversal and symlink escapes. Bound recursion, file sizes, response sizes, and execution time. Avoid loading large files or datasets entirely into memory. Never commit secrets, personal paths, or machine-specific tokens.

## Contributions

Use focused commits with imperative subjects, for example `Add bounded search results`. Pull requests should describe behavior, memory or context impact, dependencies, security implications, and tests.

After completing changes, propose a focused commit message that describes the completed work.
