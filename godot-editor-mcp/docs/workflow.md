# Feature workflow

Apply these Godot-specific rules to every feature, fix, or refactor.

1. Trace Python/Godot data, commands, errors, limits, versions, persistence, waits, and debugger identities. Validate bounded schemas on both sides of the bridge.
2. Run headless editor checks for editor/bridge changes and subprocess integration tests for cross-process changes.
3. For a release, synchronize Python and plugin metadata, runtime versions, tests, examples, and history.
