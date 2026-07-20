# Library: command handler ownership map

**Source:** `plugin/addons/godot_mcp/command_router.gd`

Services publish `Dictionary` maps from stable command strings to direct `Callable` handlers. `register_handlers(owner, handlers)` validates the entire proposed map first and rejects invalid callables, duplicate names within the map, or names already owned by another service. Only then is registration committed. `dispatch` returns a stable unknown-command envelope when absent.

The composition root retains each service instance, registers its map explicitly, and compares the final sorted command set with Python expectations through capabilities/tests.
