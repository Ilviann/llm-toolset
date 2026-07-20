# Type: operation record

**Source:** `plugin/addons/godot_mcp/operation_registry.gd`

Process-scoped record for an accepted asynchronous editor action. It carries an opaque operation ID, kind, accepted/completed state, optional run identity, concise details, and bounded recency information.

Trackers/services bind these records to observed transitions. Registry operations are documented separately in [`operation-registry-library.md`](operation-registry-library.md); do not infer completion from state alone when an operation ID exists.
