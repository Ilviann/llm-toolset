# Library: project identity and discovery selection

**Source:** `godot_editor_mcp/discovery.py`

- `normalized_project_path(project)` produces the canonical path representation with explicit POSIX/Windows behavior.
- `project_path_hash(project)` produces the SHA-256 identity mirrored by Godot.
- `read_discovery_record(project)` parses and validates the bounded heartbeat.
- `discovered_port(project, fallback)` selects a fresh matching record, safely falls back for absent/stale/malformed matching state, and rejects a record for another project.

Normalization is a compatibility contract for discovery and reload; test both platform branches regardless of the host platform.
