# Type: `Mode`

**Source:** `godot_editor_mcp/tool_catalog.py`

Literal Python type with exactly `tiny`, `small`, and `large`. Mode membership is nested and ordering is stable. The CLI default is `tiny`; the server validates calls against the active mode, not only tool listing.

When a tool changes mode, update the registry-derived public order, README tool table, capability expectations, and contract/server tests. Do not introduce ad-hoc mode checks outside catalog/server policy.
