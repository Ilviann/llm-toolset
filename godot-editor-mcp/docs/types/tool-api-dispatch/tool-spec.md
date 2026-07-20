# Type: `ToolSpec`

**Source:** `godot_editor_mcp/tool_catalog.py`

Immutable record that is the authoritative model-facing definition of one MCP tool.

| Field family | Meaning |
| --- | --- |
| Identity/presentation | Public name, description, and bounded `inputSchema`. |
| Exposure | Minimum mode and stable registry position. |
| Execution | Local handler kind or editor bridge command. |
| Policy | Project-path fields, wait behavior, and Python-only fields. |

Registry-derived maps and orders must remain unique and complete. A new spec is incomplete until schema validation, dispatcher routing, Godot handler ownership, capabilities, errors/limits, README, and contract tests agree.
