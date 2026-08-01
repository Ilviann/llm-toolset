# Tool API and dispatch contracts

Use the index to retrieve only the contract section relevant to the task.

## Type: `Mode`

**Source:** `godot_editor_mcp/tool_catalog.py`

Literal Python type with exactly `tiny`, `small`, and `large`. Mode membership is nested and ordering is stable. The CLI default is `tiny`; the server validates calls against the active mode, not only tool listing.

When a tool changes mode, update the registry-derived public order, README tool table, capability expectations, and contract/server tests. Do not introduce ad-hoc mode checks outside catalog/server policy.

## Type: `ToolSpec`

**Source:** `godot_editor_mcp/tool_catalog.py`

Immutable record that is the authoritative model-facing definition of one MCP tool.

| Field family | Meaning |
| --- | --- |
| Identity/presentation | Public name, description, and bounded `inputSchema`. |
| Exposure | Minimum mode and stable registry position. |
| Execution | Local handler kind or editor bridge command. |
| Policy | Project-path fields, wait behavior, and Python-only fields. |

Registry-derived maps and orders must remain unique and complete. A new spec is incomplete until schema validation, dispatcher routing, Godot handler ownership, capabilities, errors/limits, README, and contract tests agree.

## Types: dispatcher collaborator protocols

**Source:** `godot_editor_mcp/tool_dispatch.py`

- `BridgeClient` sends a named editor command with an argument object.
- `AssetManager` checks project paths, imports one staged file, and creates one folder.
- `EditorStarter` starts only the configured editor or reports current launch state.
- `Waiter` converts accepted editor operations into bounded completion results and supports cancellation.

`ToolDispatcher` depends on these structural protocols, not concrete classes. Preserve their narrow methods so tests can inject fakes and local services do not leak into bridge policy.

## Library: published-schema validation

**Source:** `godot_editor_mcp/schema_validation.py`

`validate_tool_arguments(value, schema)` enforces exactly the dependency-free JSON Schema subset published by the catalog: local `$ref`, exact `oneOf`, required fields, JSON scalar types, enums/constants, numeric/string/array/object bounds, patterns, nested items/properties, and `additionalProperties`.

`SchemaValidationError` reports a model-facing field path and reason. Python booleans are not accepted as integers/numbers. Non-finite numbers fail. Equality follows JSON scalar distinctions. Add a keyword only when schemas publish it, implement it recursively, and table-test normal and failing nested cases.
