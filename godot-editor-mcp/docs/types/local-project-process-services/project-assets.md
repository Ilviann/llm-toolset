# Type: `ProjectAssets`

**Source:** `godot_editor_mcp/assets.py`

Stateful Python service constructed with a configured Godot project and optional import inbox. It validates configured roots once, then exposes confined path checks, folder creation, and one-file staged import.

Inputs are project/import-root-relative identifiers using forward slashes. The service rejects absolute paths, traversal, symlink escapes, protected destinations, unsupported extensions, missing/disabled roots, oversized files, and existing/raced destinations. It returns concise paths/status for dispatch; Godot scanning remains an editor-side command.
