# Type: event record

**Source:** `plugin/addons/godot_mcp/event_store.gd`

Bounded timestamped editor event with a monotonically increasing event ID, kind, and concise details. Scene, run, import/filesystem, and related trackers use the shared sequence so clients can correlate state changes without separate clocks.

Event IDs are observation cursors, not operation IDs and not durable across editor processes. Store operations are documented separately in [`event-store-library.md`](event-store-library.md).
