# Godot Editor MCP Guidelines

Use `CODE.md`, `README.md`, executable metadata, and runtime `capabilities` as the current sources for architecture, tool modes, limits, compatibility, and behavior. Keep this `AGENTS.md` limited to durable invariants:

- Deploy the Python package and Godot plugin as an exact-version pair. Reject mismatches instead of adding compatibility paths.
- Keep the editor bridge authenticated, localhost-only, bounded, and fail-closed when token persistence fails. Only one bridge may own a configured port.
- Keep the runtime probe debugger-only with no game-side network listener, arbitrary mutation, arbitrary method calls, expressions, or supplied-code evaluation.
- Preserve project confinement, bounded operations, stable error envelopes, and atomic no-overwrite asset publication across POSIX and Windows.
- Imports are asynchronous and copy one file at a time; external `.gltf` dependencies must be imported separately, while `.glb` is self-contained.
- Verify editor API changes with headless plugin/bridge checks. macOS is the currently validated platform; record native Linux and Windows results when performed.
- A forced headless shutdown may emit the dummy-renderer `Parameter "t" is null` diagnostic after preview work. Treat it as a shutdown artifact only when plugin loading succeeded and no earlier script error exists.
