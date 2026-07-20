# Types: dispatcher collaborator protocols

**Source:** `godot_editor_mcp/tool_dispatch.py`

- `BridgeClient` sends a named editor command with an argument object.
- `AssetManager` checks project paths, imports one staged file, and creates one folder.
- `EditorStarter` starts only the configured editor or reports current launch state.
- `Waiter` converts accepted editor operations into bounded completion results and supports cancellation.

`ToolDispatcher` depends on these structural protocols, not concrete classes. Preserve their narrow methods so tests can inject fakes and local services do not leak into bridge policy.
