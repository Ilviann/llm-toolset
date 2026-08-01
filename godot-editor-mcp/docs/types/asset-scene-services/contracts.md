# Asset/scene service contracts

Use the index to retrieve only the contract section relevant to the task.

## Types: asset record and page

**Source:** `plugin/addons/godot_mcp/asset_commands.gd`

An asset record identifies a project resource by normalized `res://` path and exposes bounded type/category/import/dependency metadata appropriate to the selected command. Listing pages include `items`, filesystem-derived `snapshot_id`, `truncated`, `continuation_available`, and an opaque `cursor` when another page exists.

The cursor binds folder/type/filter/limit query and filesystem generation. Filesystem changes invalidate continuation. Scans stop at the published ceiling; truncation without continuation means the ceiling, not an unbounded server-side cache.

## Types: scene inspection records and pages

**Sources:** `edited_scene_inspector.gd`, `runtime_scene_inspector.gd`, runtime tree service

Tree records expose normalized scene-relative path, name, class, parent/depth, and bounded structural metadata; runtime records additionally expose hashed runtime identity, script/source scene, groups, process mode, and visibility. Property records expose category, exact name, Godot type, and bounded encoded value.

Pages always return explicit `scope`, stable `snapshot_id`, truncation/continuation flags, and optional cursor. Edited snapshots bind scene identity plus UndoRedo/structure/property-list state. Runtime snapshots additionally bind run, debugger session, runtime object identity, and tree generation.

## Types: scene transaction

**Source:** `plugin/addons/godot_mcp/scene_transaction.gd`; schema in `tool_catalog.py`

A request contains optional label, optimistic preconditions (`scene`, `undo_version`), and a bounded ordered operation array. Node operands contain exactly one current `path` or transaction-local `handle`. Node-creating/moving operations may bind a handle.

Supported operations are add/instantiate, set property, remove/rename/reparent, attach/detach script, connect/disconnect signal, and add/remove group. Values use the shared property-value contract.

A success result reports one committed undo version, dirty state, concise per-operation results, handles/final paths, and path transitions. Preflight or transaction failure returns a stable error with no mutation; unexpected postcondition failure is undone immediately.
