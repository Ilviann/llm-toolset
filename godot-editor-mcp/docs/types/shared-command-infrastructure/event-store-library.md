# Library: event store

**Source:** `plugin/addons/godot_mcp/event_store.gd`

Allocates the shared monotonic event sequence, timestamps concise event dictionaries, retains bounded recent history, and exposes the latest identity used by aggregate state. Trackers use this library so event ordering is comparable across state families.
