# Library: discovery heartbeat publisher

**Source:** `plugin/addons/godot_mcp/discovery_record.gd`

Publishes the mirrored `DiscoveryRecord` under `.godot` through the atomic JSON record library. `start` captures port/version/process/project identity, `poll` refreshes the heartbeat on its bounded cadence, and `stop` removes the file only when the current record still belongs to the same process.

It must start after authentication and listener ownership succeed. Never publish the token or absolute project path.
