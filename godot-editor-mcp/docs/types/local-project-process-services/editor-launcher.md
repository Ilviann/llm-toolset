# Type: `EditorLauncher`

**Source:** `godot_editor_mcp/launcher.py`

Python service constructed with the configured project, bridge, and optional
executable path selected by the composition root. The command-line
`--godot-executable` value takes precedence over `GODOT_EXECUTABLE`. `start()`
probes the bridge first, prevents repeated starts while its process is
launching, validates an absolute executable file, and starts only
`--editor --path <configured-project>`.

Return states distinguish already connected, currently starting, and newly started. Platform flags are isolated behind the Windows/POSIX branch. Launch failures use `LauncherError`; model arguments never select a program or command line.
