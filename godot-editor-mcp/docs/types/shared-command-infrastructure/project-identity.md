# Library: project identity

**Source:** `plugin/addons/godot_mcp/project_identity.gd`

Normalizes the configured project path with explicit Windows and POSIX rules and hashes the normalized string with SHA-256. Discovery and reload records use this stable identity instead of exposing an absolute path.

The algorithm mirrors `godot_editor_mcp.discovery`. Path separator, drive-letter/case, resolution, encoding, or hash changes are protocol migrations and must be tested in both languages for both platform branches.
