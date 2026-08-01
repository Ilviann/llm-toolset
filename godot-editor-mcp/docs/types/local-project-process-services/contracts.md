# Local project/process contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: `ProjectAssets`

**Source:** `godot_editor_mcp/assets.py`

Stateful Python service constructed with a configured Godot project and optional import inbox. It validates configured roots once, then exposes confined path checks, folder creation, and one-file staged import.

Inputs are project/import-root-relative identifiers using forward slashes. The service rejects absolute paths, traversal, symlink escapes, protected destinations, unsupported extensions, missing/disabled roots, oversized files, and existing/raced destinations. It returns concise paths/status for dispatch; Godot scanning remains an editor-side command.

## Type: `EditorLauncher`

**Source:** `godot_editor_mcp/launcher.py`

Python service constructed with the configured project, bridge, and optional
executable path selected by the composition root. The command-line
`--godot-executable` value takes precedence over `GODOT_EXECUTABLE`. `start()`
probes the bridge first, prevents repeated starts while its process is
launching, validates an absolute executable file, and starts only
`--editor --path <configured-project>`.

Return states distinguish already connected, currently starting, and newly started. Platform flags are isolated behind the Windows/POSIX branch. Launch failures use `LauncherError`; model arguments never select a program or command line.

## Library: atomic no-replace publication

**Source:** `godot_editor_mcp/assets.py`

The private publication helpers turn a fully written, flushed, same-directory temporary file into a destination without replacement:

- POSIX uses a same-filesystem hard link so publication fails if the destination exists.
- Windows uses native rename semantics configured to fail on an existing destination.
- The containing directory is synchronized where supported and temporary cleanup is deterministic.

Do not replace this with a check-then-rename sequence: another writer can win between validation and publication. Preserve the deterministic race and mocked Windows tests.
