# Type: cursor record

**Source:** `plugin/addons/godot_mcp/cursor_store.gd`

Private bounded record keyed by a random opaque 48-character cursor ID. It stores a read kind, normalized-query fingerprint, snapshot ID, next offset, and creation/expiry information.

At most 128 two-minute records are retained; IDs contain no token or Godot object reference. Store operations are documented separately in [`cursor-store-library.md`](cursor-store-library.md).
