# Repository Guidelines

## Purpose and Operating Environment

This repository contains lightweight tools for local LLM workflows, primarily MCP servers for LM Studio. Development and normal operation must work without ChatGPT or internet access. The target is a 16 GB MacBook Pro running small local models, so minimize memory use, startup time, dependencies, and context consumption.

## Core Design Constraints

- Prefer offline-capable, standard-library implementations.
- Do not require cloud services, telemetry, accounts, or runtime downloads.
- Keep processes small enough to run alongside LM Studio.
- Avoid background services when a short-lived or stdio process is sufficient.
- Prioritize reliable macOS behavior while using portable code where practical.
- Pin unavoidable dependencies and document how to prepare them before going offline.

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

After completing implementation work, update every affected document, README, configuration example, and roadmap before handing off the change. Update this `AGENTS.md` file too whenever the work changes repository-wide guidance, supported behavior, constraints, or known issues.

## Godot Editor MCP Known Issues

- The plugin targets Godot 4 and is verified with Godot 4.7 stable. Re-run the headless plugin load and bridge checks when changing editor APIs or claiming compatibility with another version.
- The Python server defaults to `--mode tiny`. Use `small` for asset/import workflows and `large` when the model also controls the Godot desktop. Tool calls outside the active mode are rejected as well as omitted from `tools/list`.
- Keep the Python package and installed Godot plugin on matching versions. The `capabilities` tool reports the MCP server version, bridge version, active mode, exposed tools, optional features, and effective limits.
- Godot 4.7 headless editor mode activates scenes requested through `open_scene`, so headless mutation tests can create or open a scene through the bridge before editing it.
- Source imports are asynchronous. `import_asset` and public `scan_asset` may report `queued` or `already_running`; `asset_info` can temporarily report an empty type until Godot finishes scanning. Check `editor_state.filesystem_scanning` and its generation counter before starting another full scan.
- Imports copy one file at a time. A `.gltf` with external buffers or textures requires importing every dependency separately; prefer `.glb` for a self-contained 3D asset.
- Imports never overwrite files. Destination folders must already exist or be created with `create_folder`. Move, rename, and delete operations are intentionally not exposed.
- Only one editor bridge can listen on a port. The `godot_mcp/port` project setting and MCP `--port` argument must match when changing the default port 6505.
- `create_scene` and `add_node` accept built-in Node classes only, not project script classes. `create_resource` is limited to the whitelist documented in `godot-editor-mcp/README.md`.
- Property conversion supports JSON primitives plus selected Godot types such as vectors, colors, `NodePath`, and `StringName`. Complex resource references, transforms, packed arrays, and arbitrary objects cannot currently be assigned through `set_property` or `create_resource`.
- A root-level `list_assets` includes editor addon files and may include Godot-generated import resources. Prefer a project asset folder and type filter to reduce small-model context use.
- `editor_state` exposes an absolute project path for issue identification as requested by the bridge roadmap. Other model-facing paths remain project-relative or use `res://`.
- A forced headless-editor shutdown can emit a dummy-renderer `Parameter "t" is null` diagnostic after resource preview work. Treat it as a headless shutdown artifact only when the plugin loaded successfully and no earlier script error is present.

## Security and Resource Limits

Treat model-generated arguments as untrusted. Validate types, lengths, paths, and operations. Deny access outside configured roots, including traversal and symlink escapes. Bound recursion, file sizes, response sizes, and execution time. Avoid loading large files or datasets entirely into memory. Never commit secrets, personal paths, or machine-specific tokens.

## Contributions

Use focused commits with imperative subjects, for example `Add bounded search results`. Pull requests should describe behavior, memory or context impact, dependencies, security implications, and tests.
