# Types: scene transaction

**Source:** `plugin/addons/godot_mcp/scene_transaction.gd`; schema in `tool_catalog.py`

A request contains optional label, optimistic preconditions (`scene`, `undo_version`), and a bounded ordered operation array. Node operands contain exactly one current `path` or transaction-local `handle`. Node-creating/moving operations may bind a handle.

Supported operations are add/instantiate, set property, remove/rename/reparent, attach/detach script, connect/disconnect signal, and add/remove group. Values use the shared property-value contract.

A success result reports one committed undo version, dirty state, concise per-operation results, handles/final paths, and path transitions. Preflight or transaction failure returns a stable error with no mutation; unexpected postcondition failure is undone immediately.
