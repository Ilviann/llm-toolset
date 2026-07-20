# Godot Editor MCP Guidelines

Use `docs/index.md` as the implementation-knowledge entry point. Use `README.md` for user workflows, and executable source, package/plugin metadata, behavioral tests, and runtime `capabilities` as the authoritative behavior and compatibility contracts. Keep this `AGENTS.md` limited to durable workflow rules and invariants:

- Before inspecting or changing source, start at `docs/index.md`, read the owning file in `docs/architecture/`, then read the immediate `index.md` and relevant references under `docs/types/`.
- Keep one component per `docs/architecture/*.md` file. Keep custom data types, wire records, collaborator protocols, and reusable function libraries in separate files under a component/module subfolder of `docs/types/`.
- Every directory under `docs/` must contain an `index.md`. Each index describes only its immediate files and subdirectories and links to them with relative paths.
- Update affected architecture/type/library references and their immediate indexes in the same change as source. Update `README.md` only for user-visible operation or setup. Never recreate the removed `CODE.md` monolith.
- Follow `docs/workflow.md` for working-set selection, cross-language contract review, verification, documentation, and release consistency.

- Deploy the Python package and Godot plugin as an exact-version pair. Reject mismatches instead of adding compatibility paths.
- Keep the editor bridge authenticated, localhost-only, bounded, and fail-closed when token persistence fails. Only one bridge may own a configured port.
- Keep the runtime probe debugger-only with no game-side network listener, arbitrary mutation, arbitrary method calls, expressions, or supplied-code evaluation.
- Preserve project confinement, bounded operations, stable error envelopes, and atomic no-overwrite asset publication across POSIX and Windows.
- Imports are asynchronous and copy one file at a time; external `.gltf` dependencies must be imported separately, while `.glb` is self-contained.
- Verify editor API changes with headless plugin/bridge checks. macOS is the currently validated platform; record native Linux and Windows results when performed.
- A forced headless shutdown may emit the dummy-renderer `Parameter "t" is null` diagnostic after preview work. Treat it as a shutdown artifact only when plugin loading succeeded and no earlier script error exists.
