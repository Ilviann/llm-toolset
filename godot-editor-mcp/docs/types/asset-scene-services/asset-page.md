# Types: asset record and page

**Source:** `plugin/addons/godot_mcp/asset_commands.gd`

An asset record identifies a project resource by normalized `res://` path and exposes bounded type/category/import/dependency metadata appropriate to the selected command. Listing pages include `items`, filesystem-derived `snapshot_id`, `truncated`, `continuation_available`, and an opaque `cursor` when another page exists.

The cursor binds folder/type/filter/limit query and filesystem generation. Filesystem changes invalidate continuation. Scans stop at the published ceiling; truncation without continuation means the ceiling, not an unbounded server-side cache.
