# Type: diagnostic record

**Source:** `plugin/addons/godot_mcp/diagnostic_store.gd`

Sanitized bounded record with monotonic event ID, timestamp, severity, category/scope, message, optional project-relative resource location and bounded stack frames, plus run ID for runtime output.

Queries filter by scope, severity, `since`, limit, and optional run ID without clearing history. The store retains 256 records and returns at most 100. An observation cursor older than retained history produces `stale_cursor`. C# completeness is capability-dependent; GDScript/editor/runtime capture remains the stable core.
