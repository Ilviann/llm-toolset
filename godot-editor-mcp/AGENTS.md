# Godot Editor MCP Guidelines

Start at `docs/index.md` and follow `docs/workflow.md`. Use `README.md` for user guidance; executable contracts and runtime `capabilities` remain authoritative.

- Ship the Python package and Godot plugin as an exact-version pair; reject mismatches.
- Keep the editor bridge authenticated, localhost-only, bounded, single-owner per port, and fail-closed when token persistence fails.
- Keep the debugger-only runtime probe free of game-side listeners, arbitrary mutation or method calls, expressions, and supplied-code evaluation.
- Preserve project confinement, bounded operations, stable errors, and atomic no-overwrite asset publication across platforms.
- Import one file asynchronously at a time. Import external `.gltf` dependencies separately; `.glb` is self-contained.
- Verify editor API changes with headless plugin/bridge checks. Record native platform results when run.
